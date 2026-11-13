#include "blockdropadapter.h"
#include "documentblockstore.h"
#include "waveblockcontent.h"
#include "pinloomhostclient.h"
#include <QMimeData>
#include <QImage>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringDecoder>
#include <QTextDocument>

namespace {
constexpr auto rangeMime="application/x-wave-workbench-range+json";
const QStringList codeTypes{"text/x-systemverilog","text/x-verilog","text/x-c","text/x-c++","text/x-python","text/x-code"};
bool waveObject(const QByteArray& bytes) {
    if(bytes.size()>8*1024*1024) return false;
    const auto value=QJsonDocument::fromJson(bytes).object();
    return value.value("contract")=="wave-preview/v1" || (value.contains("projectId") && value.contains("scenarios") && value.contains("schemaVersion"));
}
bool supportedLink(const QUrl& url) {
    return url.isValid() && !url.isRelative() && ((QStringList{"http","https"}.contains(url.scheme()) && !url.host().isEmpty()) || PinloomHostIdentity::fromUri(url).isValid());
}
}
bool BlockDropAdapter::accepts(const QMimeData* data) {
    if (!data) return false;
    for(const auto& type:codeTypes) if(data->hasFormat(type)) return true;
    return data->hasUrls() || data->hasImage() || data->hasText() || data->hasHtml() || data->hasFormat(rangeMime)
        || data->hasFormat("application/json") || data->hasFormat("application/x-wave-workbench-project+json") || data->hasFormat("application/x-wave-preview+json");
}
QList<EditorAssociatedBlock> BlockDropAdapter::decode(const QMimeData* data,DocumentBlockStore* store,QString* error) {
    if (error) error->clear();
    const auto fail=[&](const QString& issue) { if(error)*error=issue; return QList<EditorAssociatedBlock>{}; };
    if (!data || !store) return fail("The drop target is unavailable.");
    QList<EditorAssociatedBlock> result;
    const auto textBlock=[&](const QByteArray& bytes,const QString& type,const QString& name) {
        EditorAssociatedBlock block; block.caption=name;
        if (waveObject(bytes)) {
            if (!WaveBlockContent::validate(bytes,error)) return false;
            block.type="wave"; block.text=QString::fromUtf8(bytes); block.height=360;
        } else {
            if(bytes.size()>4*1024*1024) return false;
            QStringDecoder decoder(QStringDecoder::Utf8); const QString text=decoder(bytes);
            if (bytes.size()>4*1024*1024 || decoder.hasError() || text.contains(QChar::Null)) return false;
            block.type=type; block.text=text; block.autoHeight=true;
        }
        result.append(block); return true;
    };
    if (data->hasUrls()) {
        if (data->urls().size()>64) return fail("Drop at most 64 files or links at once.");
        for(const auto& url:data->urls()) {
            if (!url.isLocalFile()) {
                if (!supportedLink(url)) return fail("Unsupported link scheme; no blocks were inserted.");
                EditorAssociatedBlock block; block.type="reference"; block.uri=url.toString(QUrl::FullyEncoded); block.autoHeight=true; result.append(block); continue;
            }
            const auto path=url.toLocalFile(); QFile file(path);
            if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size()>50*1024*1024) return fail("The dropped file is unreadable or exceeds 50 MiB: "+path);
            const auto bytes=file.readAll(); if(file.error()!=QFileDevice::NoError) return fail(file.errorString());
            const auto suffix=QFileInfo(path).suffix().toLower(),name=QFileInfo(path).fileName();
            if (QStringList{"png","jpg","jpeg","bmp","webp","gif"}.contains(suffix)) {
                EditorAssociatedBlock block; block.type="image"; block.caption=name; block.asset=store->importImage(path,error);
                if(block.asset.isEmpty()) return {}; result.append(block);
            } else if (waveObject(bytes)) { if(!textBlock(bytes,"text",name)) return {}; }
            else {
                const bool code=QStringList{"sv","svh","v","vh","c","h","cpp","py","tcl","vhd","vhdl","js","ts","json"}.contains(suffix);
                if ((code || QStringList{"txt","md","log","csv"}.contains(suffix)) && textBlock(bytes,code ? "code" : "text",name)) continue;
                EditorAssociatedBlock block; block.type="attachment"; block.caption=name;
                block.asset=store->importBytes(bytes,suffix,error); if(block.asset.isEmpty()) return {}; result.append(block);
            }
        }
    } else if (data->hasFormat(rangeMime)) {
        // Tickx's range MIME omits its timebase. Preserve its bytes without inventing one.
        const auto bytes=data->data(rangeMime); if(bytes.size()>8*1024*1024 || !QJsonDocument::fromJson(bytes).isObject()) return fail("Invalid or oversized Tickx range data.");
        EditorAssociatedBlock block; block.type="attachment"; block.caption="Tickx range · timebase not included";
        block.asset=store->importBytes(bytes,"json",error); if(block.asset.isEmpty()) return {}; result.append(block);
    } else if(data->hasImage()) {
        const auto image=qvariant_cast<QImage>(data->imageData());
        if(image.isNull() || qint64(image.width())*image.height()>50000000) return fail("Unsupported image or excessive image dimensions.");
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
        if(!image.save(&buffer,"PNG")) return fail("The image could not be read.");
        EditorAssociatedBlock block; block.type="image"; block.asset=store->importBytes(bytes,"png",error);
        if(block.asset.isEmpty()) return {}; result.append(block);
    } else {
        QByteArray bytes; QString type="text";
        for(const auto& format:codeTypes) if(data->hasFormat(format)) { bytes=data->data(format); type="code"; break; }
        for(const auto* format:{"application/x-wave-workbench-project+json","application/x-wave-preview+json","application/json"}) if(bytes.isEmpty() && data->hasFormat(format)) bytes=data->data(format);
        if(bytes.isEmpty() && data->hasText()) bytes=data->text().toUtf8();
        if(bytes.isEmpty() && data->hasHtml()) { QTextDocument html; html.setHtml(data->html()); bytes=html.toPlainText().toUtf8(); }
        if(bytes.isEmpty()) return fail("This drop contains no readable text, image, file, link or Tickx project.");
        const QUrl link(QString::fromUtf8(bytes).trimmed(),QUrl::StrictMode);
        if(type=="text" && supportedLink(link)) { EditorAssociatedBlock block; block.type="reference"; block.uri=link.toString(QUrl::FullyEncoded); block.autoHeight=true; result.append(block); }
        else if(!textBlock(bytes,type,{})) return fail(error && !error->isEmpty() ? *error : "The dropped content is not readable UTF-8 text.");
    }
    return result;
}

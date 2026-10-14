#pragma once

#include "completiontypes.h"
#include "zeroslackexport.h"
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QTextCursor>

class QTextDocument;

struct TemplateFragment {
  int start = 0, length = 0;
  bool auxiliary = false;
  int end() const { return start + length; }
};

struct TemplateInstance {
  QString id, parentId, parentSlot, label, fixedHash;
  QVariantMap snapshot;
  QList<TemplateFragment> fragments;
  CodeTemplateSlotList inputs; // Absolute UTF-16 document positions.
};

struct TemplateInsertionSpec {
  QString label;
  QVariantMap snapshot;
  QList<TemplateFragment> fragments; // Relative to the insertion text.
  int preservedStart = -1, preservedLength = 0;
};

// One authority per QTextDocument, shared by all views. UI modes never own a
// tree.
class ZEROSLACK_API DocumentTemplateModel : public QObject {
  Q_OBJECT
public:
  static DocumentTemplateModel *get(QTextDocument *document);
  const QList<TemplateInstance> &instances() const { return items; }
  const TemplateInstance *instance(const QString &id) const;
  int depth(const QString &id) const;
  QString issue() const { return problem; }
  bool changing() const { return manual != 0 || replay; }
  int inputAt(const QString &id, int start, int end) const;
  bool allows(const QString &id, int input, int start, int end) const;
  QPair<int, int> inputSegment(const QString &id, int input,
                               int position) const;
  QString containerAt(int position, QString *inputName = nullptr) const;
  bool canInsert(int position, int removed, const QString &parentId,
                 const QString &parentSlot) const;
  bool replaceInput(const QString &id, int input, const QTextCursor &cursor,
                    const QString &text, int *nextPosition = nullptr);
  void beginChange();
  void applyChange(int position, int removed, int added,
                   const QString &owner = {}, int input = -1);
  QString add(TemplateInstance instance);
  void endChange();
  void beginReplay();
  void endReplay();
  void invalidate(const QString &reason);
  QJsonObject serialize(const QString &fileName) const;
  bool restore(const QJsonObject &object, const QString &fileName);
  void setInputContext(const QString &id,
                       std::shared_ptr<const TemplateInputContext> context);

signals:
  void changed();

private:
  explicit DocumentTemplateModel(QTextDocument *document);
  QTextDocument *text;
  QList<TemplateInstance> items;
  QMap<int, QList<TemplateInstance>> history;
  QString problem;
  int manual = 0;
  bool replay = false;
  bool pendingRecord = false;
  void remember();
  void removeTree(const QString &id);
  bool ancestor(const QString &parent, const QString &child) const;
  bool valid(const QList<TemplateInstance> &values) const;
  QString fixedDigest(const TemplateInstance &value) const;
};

#include "codetemplateservice.h"

CodeTemplateService* CodeTemplateService::getInstance() { static CodeTemplateService service; return &service; }
QList<CodeTemplateItem> CodeTemplateService::catalog() const { return {}; }
QList<CodeTemplateItem> CodeTemplateService::matchingTemplates(const QString&, const QString&, const CodeTemplateSignalContext&) const { return {}; }
CodeTemplateItem CodeTemplateService::templateForCommand(const QString&, const QString&, const CodeTemplateSignalContext&) const { return {}; }

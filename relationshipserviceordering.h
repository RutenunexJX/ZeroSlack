#ifndef RELATIONSHIPSERVICEORDERING_H
#define RELATIONSHIPSERVICEORDERING_H

#include "relationshipservice.h"

#include <QList>

namespace relationship_service_ordering {

void sortRelationshipResults(QList<RelationshipResult>& relationships, bool outgoing);

} // namespace relationship_service_ordering

#endif // RELATIONSHIPSERVICEORDERING_H

#ifndef RTLINSIGHTSGRAPHCONSTANTS_H
#define RTLINSIGHTSGRAPHCONSTANTS_H

#include <Qt>
#include <QtGlobal>

inline constexpr int kGraphKindRole = Qt::UserRole + 1200;
inline constexpr int kGraphPrimaryRole = Qt::UserRole + 1201;
inline constexpr int kGraphSecondaryRole = Qt::UserRole + 1202;
inline constexpr int kGraphFileRole = Qt::UserRole + 1203;
inline constexpr int kGraphLineRole = Qt::UserRole + 1204;
inline constexpr int kGraphColumnRole = Qt::UserRole + 1205;
inline constexpr int kGraphDrillModuleRole = Qt::UserRole + 1206;
inline constexpr int kGraphDrillFileRole = Qt::UserRole + 1207;
inline constexpr int kGraphDetailRole = Qt::UserRole + 1208;
inline constexpr int kGraphNodeIdRole = Qt::UserRole + 1209;
inline constexpr int kGraphTablePrimaryRole = Qt::UserRole + 1210;
inline constexpr int kGraphTableSecondaryRole = Qt::UserRole + 1211;
inline constexpr int kGraphTableKindRole = Qt::UserRole + 1212;
inline constexpr int kGraphBadgeRole = Qt::UserRole + 1213;
inline constexpr int kGraphEdgeLabelRole = Qt::UserRole + 1214;
inline constexpr int kGraphEdgeLabelBoldRole = Qt::UserRole + 1215;
inline constexpr int kGraphEdgeLabelBackgroundRole = Qt::UserRole + 1216;
inline constexpr int kGraphEdgeHasCurveRole = Qt::UserRole + 1217;
inline constexpr int kGraphEdgeArrowAngleRole = Qt::UserRole + 1218;
inline constexpr int kGraphEdgeLabelColorRole = Qt::UserRole + 1219;
inline constexpr int kGraphEdgeRouteKindRole = Qt::UserRole + 1220;
inline constexpr int kGraphEdgeRouteLaneRole = Qt::UserRole + 1221;
inline constexpr int kGraphEdgeLabelXRole = Qt::UserRole + 1222;
inline constexpr int kGraphEdgeLabelYRole = Qt::UserRole + 1223;
inline constexpr int kGraphEdgeLabelRectRole = Qt::UserRole + 1224;
inline constexpr int kGraphEdgeHitPriorityRole = Qt::UserRole + 1225;
inline constexpr int kGraphEdgeBridgeRole = Qt::UserRole + 1226;
inline constexpr int kGraphEdgeOverlapRole = Qt::UserRole + 1227;
inline constexpr int kGraphEdgePathLengthRole = Qt::UserRole + 1228;
inline constexpr int kGraphNodePathClearRectRole = Qt::UserRole + 1229;
inline constexpr int kGraphEdgeRouteDecisionRole = Qt::UserRole + 1230;
inline constexpr int kGraphFsmNodeIdRole = Qt::UserRole + 1231;
inline constexpr int kGraphEdgeFromNodeIdRole = Qt::UserRole + 1232;
inline constexpr int kGraphEdgeToNodeIdRole = Qt::UserRole + 1233;
inline constexpr int kGraphFsmCanonicalNodeIdRole = Qt::UserRole + 1234;
inline constexpr int kGraphHoverActiveRole = Qt::UserRole + 1235;

inline constexpr qreal kInsightNodeWidth = 170.0;
inline constexpr qreal kInsightNodeHeight = 56.0;
inline constexpr qreal kGraphMinScale = 0.05;
inline constexpr qreal kGraphMaxScale = 6.0;
inline constexpr double kPi = 3.14159265358979323846;

#endif // RTLINSIGHTSGRAPHCONSTANTS_H

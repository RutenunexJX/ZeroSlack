#pragma once

#include "zeroslackexport.h"
#include <QWidget>

class EditingTimeService;

class ZEROSLACK_API EditingTimeWidget final : public QWidget {
public:
    explicit EditingTimeWidget(EditingTimeService* service, QWidget* parent = nullptr);
};

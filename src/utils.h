// Copyright (C) 2024 The Advantech Company Ltd.
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string>

class QString;
class QWidget;

class Utils
{
public:
    static void setParentWindow(QWidget *w, const QString &parent_window);
};

std::pair<std::string, int> execute_cmd(const char *cmd);

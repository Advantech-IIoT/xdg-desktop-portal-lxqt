// Copyright (C) 2024 The Advantech Company Ltd.
// SPDX-License-Identifier: GPL-3.0-only

#include "utils.h"

#include <array>
#include <QString>
#include <QWidget>
#include <QDebug>

using namespace std;

void Utils::setParentWindow(QWidget *w, const QString &parent_window)
{
    if (parent_window.startsWith(QLatin1String("x11:"))) {
        w->setAttribute(Qt::WA_NativeWindow, true);
        KWindowSystem::setMainWindow(w->windowHandle(), parent_window.mid(4).toULongLong(nullptr, 16));
    }
    if (parent_window.startsWith((QLatin1String("wayland:")))) {
        if (!w->window()->windowHandle()) {
            w->window()->winId(); // create QWindow
        }
        KWindowSystem::setMainWindow(w->window()->windowHandle(), parent_window.mid(strlen("wayland:")));
    }
}

pair<string, int> execute_cmd(const char *cmd)
{
    array<char, 256> buffer;
    string result;
    auto pipe = popen(cmd, "r");
    if (!pipe)
    {
        qDebug("popen() failed!");
        return make_pair(result, EXIT_FAILURE);
    }
    while (!feof(pipe))
    {
        if (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
            result += buffer.data();
    }
    auto rc = pclose(pipe);
    qDebug("cmd:%s value:%s", cmd, result.c_str());
    return make_pair(result, rc);
}

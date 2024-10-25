// Copyright (C) 2024 The Advantech Company Ltd.
// SPDX-License-Identifier: GPL-3.0-only

#include "utils.h"

#include <array>
#include <QDebug>

using namespace std;

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

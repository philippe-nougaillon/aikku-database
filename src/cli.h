#pragma once

#include <QStringList>

namespace Cli {
bool isHeadlessCommand(int argc, char *argv[]);
int run(const QStringList &args);
} // namespace Cli

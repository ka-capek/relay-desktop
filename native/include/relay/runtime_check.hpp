#pragma once

#include <QProcessEnvironment>
#include <QString>

namespace relay {

enum class RuntimeTool { git, githubCli };

// These checks run in a controller worker. They never perform authentication.
[[nodiscard]] QString runtimeInstallHelp(RuntimeTool tool);
[[nodiscard]] QString systemRuntimeExecutable(RuntimeTool tool);
[[nodiscard]] QString checkRuntime(RuntimeTool tool, const QString& executable,
                                 const QProcessEnvironment& environment);

}  // namespace relay

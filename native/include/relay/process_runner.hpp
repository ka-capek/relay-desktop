#pragma once

#include <QByteArray>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <stdexcept>
#include <utility>

namespace relay {

struct ProcessRequest {
  ProcessRequest() = default;
  ProcessRequest(QString requestedProgram, QStringList requestedArguments)
      : program(std::move(requestedProgram)), arguments(std::move(requestedArguments)) {}

  QString program;
  QStringList arguments;
  QString workingDirectory;
  QByteArray standardInput;
  QProcessEnvironment environment{QProcessEnvironment::systemEnvironment()};
  int timeoutMilliseconds{120000};
  qsizetype maximumOutputBytes{20 * 1024 * 1024};
};

struct ProcessResult {
  int exitCode{};
  QProcess::ExitStatus exitStatus{QProcess::NormalExit};
  QByteArray standardOutput;
  QByteArray standardError;
};

class ProcessError final : public std::runtime_error {
 public:
  ProcessError(QString message, ProcessResult result = {});

  [[nodiscard]] const QString& qMessage() const noexcept;
  [[nodiscard]] const ProcessResult& result() const noexcept;

 private:
  QString message_;
  ProcessResult result_;
};

class ProcessRunner final {
 public:
  static ProcessResult run(const ProcessRequest& request);
};

class AsyncProcess final : public QObject {
  Q_OBJECT

 public:
  explicit AsyncProcess(QObject* parent = nullptr);
  ~AsyncProcess() override;

  void start(const ProcessRequest& request);
  void cancel();
  [[nodiscard]] bool isRunning() const;

 signals:
  void outputAvailable(QString text);
  void completed(relay::ProcessResult result);
  void failed(QString message);

 private:
  QProcess process_;
  QByteArray standardOutput_;
  QByteArray standardError_;
  qsizetype maximumOutputBytes_{};
  bool canceled_{};
  bool timedOut_{};
  QString program_;
  QTimer timeout_;
};

}  // namespace relay

Q_DECLARE_METATYPE(relay::ProcessResult)

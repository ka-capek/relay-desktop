#include "relay/process_runner.hpp"

#include <algorithm>
#include <QElapsedTimer>

namespace relay {
namespace {

QString bestError(const ProcessResult& result, const QString& fallback) {
  const auto bytes = result.standardError.isEmpty() ? result.standardOutput : result.standardError;
  const auto detail = QString::fromUtf8(bytes).trimmed();
  return detail.isEmpty() ? fallback : detail;
}

void appendBounded(QByteArray& destination, const QByteArray& value, const qsizetype maximum) {
  if (maximum <= 0) return;
  const auto remaining = std::max<qsizetype>(0, maximum - destination.size());
  destination.append(value.first(std::min(remaining, value.size())));
}


}  // namespace

ProcessError::ProcessError(QString message, ProcessResult result)
    : std::runtime_error(message.toStdString()), message_(std::move(message)), result_(std::move(result)) {}

const QString& ProcessError::qMessage() const noexcept { return message_; }
const ProcessResult& ProcessError::result() const noexcept { return result_; }

ProcessResult ProcessRunner::run(const ProcessRequest& request) {
  QProcess process;
  process.setProgram(request.program);
  process.setArguments(request.arguments);
  process.setProcessEnvironment(request.environment);
  if (!request.workingDirectory.isEmpty()) process.setWorkingDirectory(request.workingDirectory);
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start(QIODevice::ReadWrite);

  if (!process.waitForStarted()) {
    throw ProcessError(QStringLiteral("%1 could not start: %2").arg(request.program, process.errorString()));
  }

  if (!request.standardInput.isEmpty()) process.write(request.standardInput);
  process.closeWriteChannel();

  ProcessResult result;
  QElapsedTimer elapsed;
  elapsed.start();
  const auto drain = [&] {
    const auto output = process.readAllStandardOutput();
    const auto error = process.readAllStandardError();
    const auto used = result.standardOutput.size() + result.standardError.size();
    const auto available = std::max<qsizetype>(0, request.maximumOutputBytes - used);
    if (output.size() > available || error.size() > available - output.size()) {
      process.kill();
      process.waitForFinished(2000);
      throw ProcessError(QStringLiteral("Process output exceeded the limit. Narrow the requested diff or history."));
    }
    result.standardOutput.append(output);
    result.standardError.append(error);
  };
  while (process.state() != QProcess::NotRunning) {
    process.waitForReadyRead(50);
    drain();
    if (request.timeoutMilliseconds >= 0 && elapsed.elapsed() >= request.timeoutMilliseconds &&
        process.state() != QProcess::NotRunning) {
      process.kill();
      process.waitForFinished(2000);
      throw ProcessError(QStringLiteral("%1 timed out.").arg(request.program), std::move(result));
    }
  }
  drain();
  result.exitCode = process.exitCode();
  result.exitStatus = process.exitStatus();
  if (result.exitStatus != QProcess::NormalExit || result.exitCode != 0) {
    throw ProcessError(bestError(result, QStringLiteral("%1 exited with code %2.").arg(request.program).arg(result.exitCode)), result);
  }
  return result;
}

AsyncProcess::AsyncProcess(QObject* parent) : QObject(parent) {
  timeout_.setSingleShot(true);
  connect(&timeout_, &QTimer::timeout, this, [this] {
    if (!isRunning()) return;
    timedOut_ = true;
    process_.kill();
  });
  connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
    const auto value = process_.readAllStandardOutput();
    appendBounded(standardOutput_, value, maximumOutputBytes_);
    emit outputAvailable(QString::fromUtf8(value));
  });
  connect(&process_, &QProcess::readyReadStandardError, this, [this] {
    const auto value = process_.readAllStandardError();
    appendBounded(standardError_, value, maximumOutputBytes_);
    emit outputAvailable(QString::fromUtf8(value));
  });
  connect(&process_, &QProcess::errorOccurred, this, [this](const QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) emit failed(process_.errorString());
  });
  connect(&process_, &QProcess::finished, this, [this](const int code, const QProcess::ExitStatus status) {
    timeout_.stop();
    appendBounded(standardOutput_, process_.readAllStandardOutput(), maximumOutputBytes_);
    appendBounded(standardError_, process_.readAllStandardError(), maximumOutputBytes_);
    const ProcessResult result{code, status, standardOutput_, standardError_};
    if (timedOut_) emit failed(QStringLiteral("%1 timed out.").arg(program_));
    else if (canceled_) return;
    else if (status != QProcess::NormalExit || code != 0) emit failed(bestError(result, QStringLiteral("Process failed.")));
    else emit completed(result);
  });
}

AsyncProcess::~AsyncProcess() {
  if (isRunning()) {
    process_.kill();
    process_.waitForFinished(2000);
  }
}

void AsyncProcess::start(const ProcessRequest& request) {
  if (isRunning()) throw std::logic_error("AsyncProcess is already running");
  standardOutput_.clear();
  standardError_.clear();
  canceled_ = false;
  timedOut_ = false;
  program_ = request.program;
  maximumOutputBytes_ = request.maximumOutputBytes;
  process_.setProgram(request.program);
  process_.setArguments(request.arguments);
  process_.setProcessEnvironment(request.environment);
  process_.setWorkingDirectory(request.workingDirectory);
  process_.start(QIODevice::ReadWrite);
  if (!request.standardInput.isEmpty()) process_.write(request.standardInput);
  process_.closeWriteChannel();
  if (request.timeoutMilliseconds > 0) timeout_.start(request.timeoutMilliseconds);
}

void AsyncProcess::cancel() {
  if (!isRunning()) return;
  canceled_ = true;
  timeout_.stop();
  emit failed(QStringLiteral("Operation canceled."));
  process_.kill();
}

bool AsyncProcess::isRunning() const { return process_.state() != QProcess::NotRunning; }

}  // namespace relay

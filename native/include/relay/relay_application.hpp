#pragma once

#include <QApplication>

namespace relay {

class RelayApplication final : public QApplication {
  Q_OBJECT

 public:
  RelayApplication(int& argc, char** argv);
};

}  // namespace relay

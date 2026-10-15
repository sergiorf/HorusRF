// SPDX-License-Identifier: GPL-3.0-only
#include "main_window.hpp"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("HorusRF Studio"));
    QCoreApplication::setOrganizationName(QStringLiteral("HorusRF"));

    horusrf::studio::MainWindow window;
    window.show();
    return application.exec();
}

# Third-party notices

The HorusRF core and CLI are licensed under the Apache License 2.0 as described
in [`LICENSE`](LICENSE). The separate HorusRF Studio application and its tests
are GPL-3.0-only as described in [`LICENSE-GPL-3.0`](LICENSE-GPL-3.0).
Third-party software remains under its own license; it is not relicensed by
HorusRF.

## Qt 6

HorusRF Studio uses the Qt 6 Core, GUI, Widgets, and Charts modules, with Qt Test
used only by Studio tests. HorusRF does not include Qt source code or binaries in this
repository. These Qt modules are available from The Qt Company under commercial
terms and applicable open-source terms. Core, GUI, Widgets, and Test are
available under LGPLv3 terms; Qt Charts is available to open-source users under
GPLv3 in the Qt version used for this project.

Open-source builds of HorusRF Studio are intended to link dynamically to Qt.
Anyone distributing HorusRF Studio together with Qt is responsible for meeting
the applicable Qt and third-party license obligations. These include providing
required notices and corresponding Studio source and build scripts, making
corresponding Qt source available as required, and meeting replacement or
relinking requirements that apply to the selected Qt modules. Consult the
license information shipped with the exact Qt version being distributed:

- <https://doc.qt.io/qt-6/licensing.html>
- <https://doc.qt.io/qt-6/lgpl.html>
- <https://doc.qt.io/qt-6/licenses-used-in-qt.html>

The GPL-3.0-only Studio boundary does not relicense the independent Apache-2.0
core or `horusrf-run` process. A commercial Qt license may be used instead,
subject to its terms.

This notice is informational and is not legal advice.

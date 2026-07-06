// --- START OF FILE: HexaStudio/overlays/SettingsPanel.cpp ---
#include "SettingsPanel.h"
#include "OverlayWidgets.h"

#include "HexaTheme.h"
#include "HexaWidgets.h"

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace hexa {

namespace {
constexpr int kFrameWidthPx = 960;
constexpr int kFrameHeightPx = 600;
constexpr int kNavWidthPx = 190;
constexpr int kDimAlpha = 200;          // overlay dim behind the frame (matches the shipping look)
constexpr int kFrameSlotCount = 10;     // tool/base slots, same contract as the shipping overlay
} // namespace

SettingsPanel::SettingsPanel(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setupUi();
    m_listCategories->setCurrentRow(CategoryNetwork);
}

// Config is PUSHED by the host (MainWindow at integration, the fake on the bench). The shipping
// overlay pulled the RobotService singleton in showEvent - that reach is gone.
void SettingsPanel::setConfig(const HmiSystemConfig& config) {
    m_tempConfig = config;
    rebuildEditor(m_lastCategory);   // re-read the staged values into the visible editor
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void SettingsPanel::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setAlignment(Qt::AlignCenter);
    mainLayout->setContentsMargins(50, 50, 50, 50);

    QFrame* frame = new QFrame();
    QString frameStyle = Hexa::Styles::PanelMain;
    frameStyle.replace("border-radius: " + QString::number(Hexa::Dim::Radius) + "px;",
                       "border-radius: 12px;");
    frame->setStyleSheet(frameStyle);
    frame->setFixedSize(kFrameWidthPx, kFrameHeightPx);

    QVBoxLayout* frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(20, 20, 20, 20);
    frameLayout->setSpacing(10);

    // --- Header: title + apply-status chip ---
    QHBoxLayout* headerLayout = new QHBoxLayout();
    QLabel* lblTitle = new QLabel("SYSTEM CONFIGURATION");
    lblTitle->setStyleSheet(Hexa::Styles::LabelHeaderSimple + "font-size: 16px;");
    headerLayout->addWidget(lblTitle, 1);
    m_lblApplyStatus = new QLabel("Ready");
    m_lblApplyStatus->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 11px; padding: 5px 10px; background-color: rgba(255,255,255,0.06);"
        " border-radius: 8px; border: none;").arg(Hexa::Colors::TextMuted));
    headerLayout->addWidget(m_lblApplyStatus, 0);
    frameLayout->addLayout(headerLayout);
    frameLayout->addWidget(HexaWidgets::createSeparatorH());

    // --- Body: navigation column + editor area ---
    QHBoxLayout* bodyLayout = new QHBoxLayout();
    bodyLayout->setSpacing(15);

    QVBoxLayout* navLayout = new QVBoxLayout();
    navLayout->setSpacing(8);
    m_listCategories = new QListWidget();
    m_listCategories->setObjectName(QStringLiteral("listCategories"));
    m_listCategories->setFixedWidth(kNavWidthPx);
    m_listCategories->setStyleSheet(Hexa::Styles::ListView + QStringLiteral(
        "QListView { font-family: '%1'; font-size: 12px; }"
        "QListView::item { padding: 12px 10px; color: %2; letter-spacing: 1px; }")
        .arg(Hexa::Fonts::familyUI(), Hexa::Colors::TextMain));
    // REAL categories only - selecting a row always shows an editor. The shipping overlay put HAL
    // and CALIB into this list as fake rows that bounced the selection back; they are explicit
    // navigation buttons below instead.
    m_listCategories->addItems({"NETWORK", "AXIS LIMITS", "TOOLS", "BASES", "ROBOT VISUAL"});
    connect(m_listCategories, &QListWidget::currentRowChanged,
            this, &SettingsPanel::onCategoryChanged);
    navLayout->addWidget(m_listCategories, 1);

    QPushButton* btnHal = HexaWidgets::createButtonStd("HAL RUNTIME", this, kNavWidthPx, 36);
    btnHal->setObjectName(QStringLiteral("btnHalRuntime"));
    connect(btnHal, &QPushButton::clicked, this, &SettingsPanel::halOverlayRequested);
    navLayout->addWidget(btnHal);
    bodyLayout->addLayout(navLayout);

    m_scrollEditor = new QScrollArea();
    m_scrollEditor->setWidgetResizable(true);
    m_scrollEditor->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
        " QScrollArea > QWidget > QWidget { background: transparent; }");
    m_scrollEditor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bodyLayout->addWidget(m_scrollEditor, 1);
    frameLayout->addLayout(bodyLayout, 1);

    // --- Footer: CLOSE / APPLY ---
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_btnClose = HexaWidgets::createButtonStd("CLOSE", this, 120, 40);
    m_btnClose->setObjectName(QStringLiteral("btnClose"));
    connect(m_btnClose, &QPushButton::clicked, this, &SettingsPanel::closeRequested);
    btnLayout->addWidget(m_btnClose);
    m_btnApply = HexaWidgets::createButtonStd("APPLY", this, 150, 40);
    m_btnApply->setObjectName(QStringLiteral("btnApply"));
    m_btnApply->setStyleSheet(Hexa::Styles::ButtonBase +
        QStringLiteral("QPushButton { border: 1px solid %1; color: white; }").arg(Hexa::Colors::Primary) +
        Hexa::Styles::ButtonInteract);
    connect(m_btnApply, &QPushButton::clicked, this, &SettingsPanel::onApplyClicked);
    btnLayout->addWidget(m_btnApply);
    frameLayout->addLayout(btnLayout);

    mainLayout->addWidget(frame);
}

void SettingsPanel::onCategoryChanged(int row) {
    m_lastCategory = row;
    rebuildEditor(row);
}

// Editors are rebuilt on switch (the proven shipping pattern): each build reads the current staged
// values from m_tempConfig, so no per-page refresh logic is needed.
void SettingsPanel::rebuildEditor(int category) {
    if (m_scrollEditor == nullptr) return;
    if (m_scrollEditor->widget() != nullptr) m_scrollEditor->widget()->deleteLater();
    QWidget* editor = nullptr;
    switch (category) {
    case CategoryNetwork:     editor = createNetworkEditor(); break;
    case CategoryLimits:      editor = createLimitsEditor(); break;
    case CategoryTools:       editor = createToolsEditor(); break;
    case CategoryBases:       editor = createBasesEditor(); break;
    case CategoryRobotVisual: editor = createRobotVisualEditor(); break;
    default:                  editor = new QWidget(); break;
    }
    m_scrollEditor->setWidget(editor);
}

// ---------------------------------------------------------------------------
// Themed building blocks
// ---------------------------------------------------------------------------

QDoubleSpinBox* SettingsPanel::createSpin(double min, double max, int decimals, double step,
                                          double value, const std::function<void(double)>& setter) {
    QDoubleSpinBox* sb = new QDoubleSpinBox();
    sb->setRange(min, max);
    sb->setDecimals(decimals);
    sb->setSingleStep(step);
    sb->setValue(value);
    sb->setStyleSheet(overlay::fieldStyle());
    sb->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    connect(sb, &QDoubleSpinBox::valueChanged, this, setter);
    return sb;
}

// ---------------------------------------------------------------------------
// Editors
// ---------------------------------------------------------------------------

QWidget* SettingsPanel::createNetworkEditor() {
    QWidget* w = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(w);
    layout->setSpacing(10);
    layout->addWidget(overlay::createCaption(
        "Connection parameters of the robot controller."));

    QVBoxLayout* content = nullptr;
    QFrame* card = overlay::createCard("CONTROLLER ENDPOINT", &content);
    QGridLayout* grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(1, 1);

    grid->addWidget(HexaWidgets::createLabelText("IP address"), 0, 0);
    QLineEdit* edIp = new QLineEdit(m_tempConfig.network.controllerIp);
    edIp->setObjectName(QStringLiteral("editNetworkIp"));
    edIp->setStyleSheet(overlay::fieldStyle());
    connect(edIp, &QLineEdit::textChanged, this,
            [this](const QString& v) { m_tempConfig.network.controllerIp = v; });
    grid->addWidget(edIp, 0, 1);

    grid->addWidget(HexaWidgets::createLabelText("Port"), 1, 0);
    QSpinBox* sbPort = new QSpinBox();
    sbPort->setRange(1, 65535);
    sbPort->setValue(m_tempConfig.network.controllerPort);
    sbPort->setStyleSheet(overlay::fieldStyle());
    sbPort->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    connect(sbPort, &QSpinBox::valueChanged, this,
            [this](int v) { m_tempConfig.network.controllerPort = v; });
    grid->addWidget(sbPort, 1, 1);

    content->addLayout(grid);
    layout->addWidget(card);
    // NOTE: the realtime HAL backend selector lives in the HAL RUNTIME overlay (hal_control
    // module), next to the transport card it gates - moved there by boss directive 2026-07-06.
    layout->addStretch();
    return w;
}

QWidget* SettingsPanel::createLimitsEditor() {
    QWidget* w = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(w);
    layout->setSpacing(10);
    layout->addWidget(overlay::createCaption("Soft limits for the robot joints, in degrees."));

    QVBoxLayout* content = nullptr;
    QFrame* card = overlay::createCard("SOFT LIMITS (DEG)", &content);
    QGridLayout* grid = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);

    QLabel* headMin = overlay::createCaption("MIN");
    headMin->setAlignment(Qt::AlignHCenter);
    QLabel* headMax = overlay::createCaption("MAX");
    headMax->setAlignment(Qt::AlignHCenter);
    grid->addWidget(headMin, 0, 1);
    grid->addWidget(headMax, 0, 2);

    for (int i = 0; i < 6; ++i) {
        HmiAxisLimit limit{i, -170.0, 170.0};
        if (i < m_tempConfig.axisLimits.size()) limit = m_tempConfig.axisLimits[i];
        else m_tempConfig.axisLimits.append(limit);

        grid->addWidget(HexaWidgets::createLabelAxis("A" + QString::number(i + 1)), i + 1, 0);
        grid->addWidget(createSpin(-360.0, 360.0, 1, 1.0, limit.minDeg,
                                   [this, i](double v) { m_tempConfig.axisLimits[i].minDeg = v; }),
                        i + 1, 1);
        grid->addWidget(createSpin(-360.0, 360.0, 1, 1.0, limit.maxDeg,
                                   [this, i](double v) { m_tempConfig.axisLimits[i].maxDeg = v; }),
                        i + 1, 2);
    }
    content->addLayout(grid);
    layout->addWidget(card);
    layout->addStretch();
    return w;
}

QWidget* SettingsPanel::createToolsEditor() {
    return createFrameEditor(m_tempConfig.tools, QStringLiteral("TOOL"), QStringLiteral("Tool"));
}

QWidget* SettingsPanel::createBasesEditor() {
    return createFrameEditor(m_tempConfig.bases, QStringLiteral("BASE"), QStringLiteral("Base"));
}

// Editor for the structurally-identical Tool/Base frame lists. `items` references the long-lived
// m_tempConfig member, so the lambdas capture a stable pointer to it (same proven pattern as the
// shipping overlay - only the presentation changed).
template <class T>
QWidget* SettingsPanel::createFrameEditor(QVector<T>& items, const QString& kindUpper,
                                          const QString& kindTitle) {
    QVector<T>* vec = &items;
    QWidget* w = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(w);
    layout->setSpacing(10);
    layout->addWidget(overlay::createCaption(
        QStringLiteral("%1 frame definitions (offset X/Y/Z in mm, Rx/Ry/Rz in deg). %2 slots.")
            .arg(kindTitle).arg(kFrameSlotCount)));

    QComboBox* combo = HexaWidgets::createComboBox();
    combo->setObjectName(QStringLiteral("comboFrameSlot"));
    combo->setMinimumHeight(32);
    for (int i = 0; i < kFrameSlotCount; ++i) {
        QString name = "Empty Slot " + QString::number(i);
        for (const T& item : *vec) {
            if (item.id == i) { name = item.name; break; }
        }
        combo->addItem(QStringLiteral("ID %1: %2").arg(i).arg(name), i);
    }
    layout->addWidget(combo);

    QVBoxLayout* content = nullptr;
    QFrame* card = overlay::createCard(kindUpper + " DEFINITION", &content);
    QGridLayout* grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);
    content->addLayout(grid);
    layout->addWidget(card);

    auto refreshFields = [this, vec, grid, combo, kindUpper, kindTitle]() {
        QLayoutItem* child = nullptr;
        while ((child = grid->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
        }
        const int currentId = combo->currentData().toInt();
        int idx = -1;
        for (int i = 0; i < vec->size(); ++i) {
            if ((*vec)[i].id == currentId) { idx = i; break; }
        }
        if (idx == -1) {
            QPushButton* btnCreate = HexaWidgets::createButtonStd("CREATE " + kindUpper, nullptr, 200, 40);
            QObject::connect(btnCreate, &QPushButton::clicked, this,
                             [vec, currentId, combo, kindTitle]() {
                T item;
                item.id = currentId;
                item.name = "New " + kindTitle;
                item.offset.fill(0.0, 6);
                vec->append(item);
                combo->setItemText(currentId,
                                   QStringLiteral("ID %1: New %2").arg(currentId).arg(kindTitle));
                emit combo->currentIndexChanged(currentId);
            });
            grid->addWidget(btnCreate, 0, 0, 1, 6, Qt::AlignCenter);
            return;
        }

        grid->addWidget(HexaWidgets::createLabelText("Name"), 0, 0);
        QLineEdit* edName = new QLineEdit((*vec)[idx].name);
        edName->setStyleSheet(overlay::fieldStyle());
        QObject::connect(edName, &QLineEdit::textChanged, this,
                         [vec, idx, combo, currentId](const QString& v) {
            (*vec)[idx].name = v;
            combo->setItemText(currentId, QStringLiteral("ID %1: %2").arg(currentId).arg(v));
        });
        grid->addWidget(edName, 0, 1, 1, 5);

        const QStringList offsetLabels = {"X", "Y", "Z", "Rx", "Ry", "Rz"};
        for (int i = 0; i < offsetLabels.size(); ++i) {
            const int row = 1 + (i / 3);
            const int col = (i % 3) * 2;
            grid->addWidget(HexaWidgets::createLabelText(offsetLabels[i]), row, col);
            grid->addWidget(createSpin(-9999.0, 9999.0, 2, 1.0, (*vec)[idx].offset[i],
                                       [vec, idx, i](double v) { (*vec)[idx].offset[i] = v; }),
                            row, col + 1);
        }

        if (currentId != 0) {   // frame 0 is the identity default and cannot be deleted
            QPushButton* btnDelete = HexaWidgets::createButtonDanger("DELETE", nullptr, 100, 32);
            QObject::connect(btnDelete, &QPushButton::clicked, this,
                             [vec, idx, combo, currentId]() {
                vec->removeAt(idx);
                combo->setItemText(currentId, QStringLiteral("ID %1: Empty Slot %1").arg(currentId));
                emit combo->currentIndexChanged(currentId);
            });
            grid->addWidget(btnDelete, 3, 0, 1, 6, Qt::AlignRight);
        }
    };
    connect(combo, &QComboBox::currentIndexChanged, this, refreshFields);
    refreshFields();
    layout->addStretch();
    return w;
}

QWidget* SettingsPanel::createRobotVisualEditor() {
    QWidget* w = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(w);
    layout->setSpacing(10);
    layout->addWidget(overlay::createCaption(
        "URDF path and the robot base orientation (world->base). Use Rx +90 for Y-up exports, 0 for "
        "Z-up URDFs. The 3D view updates immediately; robot MOTION uses this only after a controller "
        "restart."));

    QVBoxLayout* urdfContent = nullptr;
    QFrame* urdfCard = overlay::createCard("URDF MODEL", &urdfContent);
    QHBoxLayout* urdfRow = new QHBoxLayout();
    urdfRow->setSpacing(8);
    QLineEdit* edUrdf = new QLineEdit(m_tempConfig.robotUrdfPath);
    edUrdf->setStyleSheet(overlay::fieldStyle());
    connect(edUrdf, &QLineEdit::textChanged, this,
            [this](const QString& v) { m_tempConfig.robotUrdfPath = QDir::cleanPath(v); });
    urdfRow->addWidget(edUrdf, 1);
    QPushButton* btnBrowse = HexaWidgets::createButtonSm("BROWSE", nullptr, 90, 30);
    connect(btnBrowse, &QPushButton::clicked, this, [this, edUrdf]() {
        const QString start = m_tempConfig.robotUrdfPath.isEmpty()
                                  ? QDir::currentPath()
                                  : QFileInfo(m_tempConfig.robotUrdfPath).absolutePath();
        const QString selected = QFileDialog::getOpenFileName(
            this, tr("Select URDF file"), start, tr("URDF files (*.urdf);;All files (*.*)"));
        if (!selected.isEmpty()) edUrdf->setText(QDir::cleanPath(selected));
    });
    urdfRow->addWidget(btnBrowse);
    urdfContent->addLayout(urdfRow);
    layout->addWidget(urdfCard);

    QVBoxLayout* poseContent = nullptr;
    QFrame* poseCard = overlay::createCard("ROOT TRANSFORM (MM / DEG)", &poseContent);
    QGridLayout* grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);

    struct PoseField {
        const char* label;
        double min;
        double max;
        double step;
        double value;
        std::function<void(double)> setter;
    };
    const QVector<PoseField> fields = {
        {"X",  -5000.0, 5000.0, 1.0, m_tempConfig.modelRootX,
         [this](double v) { m_tempConfig.modelRootX = v; }},
        {"Y",  -5000.0, 5000.0, 1.0, m_tempConfig.modelRootY,
         [this](double v) { m_tempConfig.modelRootY = v; }},
        {"Z",  -5000.0, 5000.0, 1.0, m_tempConfig.modelRootZ,
         [this](double v) { m_tempConfig.modelRootZ = v; }},
        {"Rx", -180.0, 180.0, 5.0, m_tempConfig.modelRootRxDeg,
         [this](double v) { m_tempConfig.modelRootRxDeg = v; }},
        {"Ry", -180.0, 180.0, 5.0, m_tempConfig.modelRootRyDeg,
         [this](double v) { m_tempConfig.modelRootRyDeg = v; }},
        {"Rz", -180.0, 180.0, 5.0, m_tempConfig.modelRootRzDeg,
         [this](double v) { m_tempConfig.modelRootRzDeg = v; }},
    };
    for (int i = 0; i < fields.size(); ++i) {
        const int row = i / 3;
        const int col = (i % 3) * 2;
        grid->addWidget(HexaWidgets::createLabelText(fields[i].label), row, col);
        grid->addWidget(createSpin(fields[i].min, fields[i].max, 1, fields[i].step,
                                   fields[i].value, fields[i].setter),
                        row, col + 1);
    }
    poseContent->addLayout(grid);
    layout->addWidget(poseCard);
    layout->addStretch();
    return w;
}

// ---------------------------------------------------------------------------
// Intent out
// ---------------------------------------------------------------------------

void SettingsPanel::onApplyClicked() {
    m_lblApplyStatus->setText("Settings applied");
    m_lblApplyStatus->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 11px; padding: 5px 10px; background-color: rgba(255,215,0,0.12);"
        " border-radius: 8px; border: none;").arg(Hexa::Colors::Warning));
    emit applyRequested(m_tempConfig);
}

void SettingsPanel::mousePressEvent(QMouseEvent* event) {
    event->accept();   // the overlay swallows clicks; nothing behind it may react
}

void SettingsPanel::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, kDimAlpha));
}

} // namespace hexa
// --- END OF FILE: HexaStudio/overlays/SettingsPanel.cpp ---

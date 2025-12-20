#include "settingsoverlay.h"
#include "styles/HexaTheme.h"
#include "styles/HexaWidgets.h"
#include "backend/RobotService.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QPainter>
#include <QMouseEvent>
#include <QDebug>
#include <QComboBox>
#include <QGroupBox>

SettingsOverlay::SettingsOverlay(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    hide();
    setupUi();

    if(m_listCategories->count() > 0)
        m_listCategories->setCurrentRow(0);
}

void SettingsOverlay::setConfig(const HmiSystemConfig &config)
{
    m_tempConfig = config;
    onCategoryChanged(m_listCategories->currentRow());
}

void SettingsOverlay::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    setConfig(RobotService::instance()->getConfig());
}

void SettingsOverlay::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setAlignment(Qt::AlignCenter);
    mainLayout->setContentsMargins(50, 50, 50, 50);

    QFrame *frame = new QFrame();
    QString frameStyle = Hexa::Styles::PanelMain;
    frameStyle.replace("border-radius: " + QString::number(Hexa::Dim::Radius) + "px;", "border-radius: 12px;");
    frame->setStyleSheet(frameStyle);
    frame->setFixedSize(900, 600);

    QVBoxLayout *frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(20, 20, 20, 20);
    frameLayout->setSpacing(10);

    QLabel *lblTitle = new QLabel("SYSTEM CONFIGURATION");
    lblTitle->setStyleSheet(Hexa::Styles::LabelHeaderSimple + "font-size: 16px; border-bottom: 1px solid " + Hexa::Colors::Border + "; padding-bottom: 10px;");
    lblTitle->setAlignment(Qt::AlignLeft);
    frameLayout->addWidget(lblTitle);

    QHBoxLayout *bodyLayout = new QHBoxLayout();
    bodyLayout->setSpacing(15);

    m_listCategories = new QListWidget();
    m_listCategories->setFixedWidth(200);
    m_listCategories->setStyleSheet(Hexa::Styles::ListView + "font-size: 14px; padding: 5px;");
    m_listCategories->addItem("NETWORK");
    m_listCategories->addItem("AXIS LIMITS");
    m_listCategories->addItem("TOOLS");
    m_listCategories->addItem("BASES");
    connect(m_listCategories, &QListWidget::currentRowChanged, this, &SettingsOverlay::onCategoryChanged);
    bodyLayout->addWidget(m_listCategories);

    m_scrollEditor = new QScrollArea();
    m_scrollEditor->setWidgetResizable(true);
    m_scrollEditor->setStyleSheet("QScrollArea { background: transparent; border: none; } QWidget { background: transparent; }");
    m_scrollEditor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bodyLayout->addWidget(m_scrollEditor, 1);

    QFrame *infoPanel = new QFrame();
    infoPanel->setFixedWidth(220);
    infoPanel->setStyleSheet("background-color: rgba(0,0,0,0.2); border-left: 1px solid " + Hexa::Colors::Border + ";");
    QVBoxLayout *infoLayout = new QVBoxLayout(infoPanel);
    m_lblInfoTitle = HexaWidgets::createLabelHeader("INFO");
    m_lblInfoTitle->setStyleSheet("color: " + Hexa::Colors::Primary + "; font-size: 12px; font-weight: bold;");
    m_lblInfoText = new QLabel("Select a category to edit settings.");
    m_lblInfoText->setWordWrap(true);
    m_lblInfoText->setStyleSheet("color: " + Hexa::Colors::TextMuted + "; font-size: 11px;");
    m_lblInfoText->setAlignment(Qt::AlignTop);

    infoLayout->addWidget(m_lblInfoTitle);
    infoLayout->addWidget(m_lblInfoText, 1);
    bodyLayout->addWidget(infoPanel);

    frameLayout->addLayout(bodyLayout, 1);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_btnCancel = HexaWidgets::createButtonStd("CANCEL", this, 120, 40);
    m_btnCancel->setStyleSheet(Hexa::Styles::ButtonBase);
    connect(m_btnCancel, &QPushButton::clicked, this, &SettingsOverlay::onCancelClicked);

    m_btnApply = HexaWidgets::createButtonStd("APPLY", this, 150, 40);
    connect(m_btnApply, &QPushButton::clicked, this, &SettingsOverlay::onApplyClicked);

    btnLayout->addWidget(m_btnCancel);
    btnLayout->addWidget(m_btnApply);
    frameLayout->addLayout(btnLayout);

    mainLayout->addWidget(frame);
}

void SettingsOverlay::onCategoryChanged(int row)
{
    if (m_scrollEditor->widget()) {
        m_scrollEditor->widget()->deleteLater();
    }

    QWidget *editor = nullptr;

    switch(row) {
    case 0: editor = createNetworkEditor(); updateInfoPanel(0); break;
    case 1: editor = createLimitsEditor(); updateInfoPanel(1); break;
    case 2: editor = createToolsEditor(); updateInfoPanel(2); break;
    case 3: editor = createBasesEditor(); updateInfoPanel(3); break;
    default: editor = new QWidget(); updateInfoPanel(-1);
    }

    if (editor) {
        m_scrollEditor->setWidget(editor);
    }
}

void SettingsOverlay::updateInfoPanel(int categoryIndex)
{
    switch(categoryIndex) {
    case 0:
        m_lblInfoTitle->setText("NETWORK");
        m_lblInfoText->setText("Configure connection parameters for the Robot Controller.\n\n"
                               "Ensure the IP address matches the physical controller's settings.\n"
                               "Port 30002 is standard for HexaKinetica real-time control.");
        break;
    case 1:
        m_lblInfoTitle->setText("AXIS LIMITS");
        m_lblInfoText->setText("Soft limits for robot joints.\n\n"
                               "These limits prevent the robot from hitting physical stops or cables.\n"
                               "Values are in Degrees.");
        break;
    case 2:
        m_lblInfoTitle->setText("TOOLS");
        m_lblInfoText->setText("Tool Center Point (TCP) definitions.\n\n"
                               "10 Slots Available.\n"
                               "Create new tool or edit existing offsets.\n"
                               "Tool 0 is always the Flange.");
        break;
    case 3:
        m_lblInfoTitle->setText("BASES");
        m_lblInfoText->setText("User Frames (Bases).\n\n"
                               "10 Slots Available.\n"
                               "Define reference frames relative to World (Base 0).");
        break;
    default:
        m_lblInfoTitle->setText("INFO");
        m_lblInfoText->setText("...");
    }
}

QWidget* SettingsOverlay::createNetworkEditor()
{
    QWidget *w = new QWidget();
    QFormLayout *layout = new QFormLayout(w);
    layout->setLabelAlignment(Qt::AlignRight);
    layout->setSpacing(15);

    QLineEdit *edIp = new QLineEdit(m_tempConfig.network.controllerIp);
    edIp->setStyleSheet(Hexa::Styles::ComboBox);
    connect(edIp, &QLineEdit::textChanged, [this](const QString &v){ m_tempConfig.network.controllerIp = v; });
    layout->addRow(HexaWidgets::createLabelText("Controller IP:"), edIp);

    QLineEdit *edPort = new QLineEdit(QString::number(m_tempConfig.network.controllerPort));
    edPort->setStyleSheet(Hexa::Styles::ComboBox);
    connect(edPort, &QLineEdit::textChanged, [this](const QString &v){ m_tempConfig.network.controllerPort = v.toInt(); });
    layout->addRow(HexaWidgets::createLabelText("Command Port:"), edPort);

    QLineEdit *edHmiIp = new QLineEdit(m_tempConfig.network.hmiIp.isEmpty() ? "127.0.0.1" : m_tempConfig.network.hmiIp);
    edHmiIp->setReadOnly(true);
    edHmiIp->setStyleSheet("QLineEdit { background: transparent; color: #666; border: none; }");
    layout->addRow(HexaWidgets::createLabelText("Local Interface:"), edHmiIp);

    return w;
}

QWidget* SettingsOverlay::createLimitsEditor()
{
    QWidget *w = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(w);
    layout->setSpacing(10);

    for (int i=0; i<6; ++i) {
        QGroupBox *grp = new QGroupBox("Joint A" + QString::number(i+1));
        grp->setStyleSheet(QString("QGroupBox { color: %1; border: 1px solid %2; margin-top: 20px; } QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; }").arg(Hexa::Colors::Accent, Hexa::Colors::SurfaceLight));

        QHBoxLayout *gl = new QHBoxLayout(grp);

        auto createSpin = [&](double val, const std::function<void(double)> &setter) {
            QDoubleSpinBox *sb = new QDoubleSpinBox();
            sb->setRange(-360.0, 360.0);
            sb->setValue(val);
            sb->setStyleSheet("background-color: rgba(0,0,0,0.3); color: white; border: 1px solid #444;");
            connect(sb, &QDoubleSpinBox::valueChanged, setter);
            return sb;
        };

        HmiAxisLimit limit = {i, -170, 170, 100};
        if (i < m_tempConfig.axisLimits.size()) limit = m_tempConfig.axisLimits[i];
        else m_tempConfig.axisLimits.append(limit);

        gl->addWidget(HexaWidgets::createLabelText("Min:"));
        gl->addWidget(createSpin(limit.minDeg, [this, i](double v){ m_tempConfig.axisLimits[i].minDeg = v; }));

        gl->addWidget(HexaWidgets::createLabelText("Max:"));
        gl->addWidget(createSpin(limit.maxDeg, [this, i](double v){ m_tempConfig.axisLimits[i].maxDeg = v; }));

        layout->addWidget(grp);
    }
    return w;
}

QWidget* SettingsOverlay::createToolsEditor()
{
    QWidget *w = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(w);

    QComboBox *comboTools = HexaWidgets::createComboBox();

    // Ensure we have 10 slots visually
    for (int i=0; i<10; ++i) {
        QString name = "Empty Slot " + QString::number(i);
        // Find if this slot is populated in config
        for(const auto& t : m_tempConfig.tools) {
            if(t.id == i) { name = t.name; break; }
        }
        comboTools->addItem(QString("ID %1: %2").arg(i).arg(name), i);
    }
    layout->addWidget(comboTools);

    QFrame *fieldsFrame = new QFrame();
    QGridLayout *grid = new QGridLayout(fieldsFrame);
    layout->addWidget(fieldsFrame);

    auto refreshFields = [this, grid, comboTools]() {
        QLayoutItem *child;
        while ((child = grid->takeAt(0)) != nullptr) {
            delete child->widget(); delete child;
        }

        int currentId = comboTools->currentData().toInt();
        int toolIdx = -1;
        for(int i=0; i<m_tempConfig.tools.size(); ++i) {
            if (m_tempConfig.tools[i].id == currentId) { toolIdx = i; break; }
        }

        bool exists = (toolIdx != -1);

        if (!exists) {
            QPushButton* btnCreate = HexaWidgets::createButtonStd("CREATE TOOL", nullptr, 200, 40);
            connect(btnCreate, &QPushButton::clicked, [this, currentId, comboTools](){
                HmiToolData newTool;
                newTool.id = currentId;
                newTool.name = "New Tool";
                newTool.offset.fill(0.0, 6);
                m_tempConfig.tools.append(newTool);
                // Refresh
                comboTools->setItemText(currentId, QString("ID %1: New Tool").arg(currentId));
                emit comboTools->currentIndexChanged(currentId);
            });
            grid->addWidget(btnCreate, 0, 0, 1, 3, Qt::AlignCenter);
        } else {
            // Name Field
            grid->addWidget(HexaWidgets::createLabelText("Name:"), 0, 0);
            QLineEdit *edName = new QLineEdit(m_tempConfig.tools[toolIdx].name);
            edName->setStyleSheet(Hexa::Styles::ComboBox);
            connect(edName, &QLineEdit::textChanged, [this, toolIdx, comboTools, currentId](const QString &v){
                m_tempConfig.tools[toolIdx].name = v;
                comboTools->setItemText(currentId, QString("ID %1: %2").arg(currentId).arg(v));
            });
            grid->addWidget(edName, 0, 1, 1, 3);

            // Offsets
            QStringList labels = {"X", "Y", "Z", "Rx", "Ry", "Rz"};
            for(int i=0; i<6; ++i) {
                grid->addWidget(HexaWidgets::createLabelText(labels[i]), 1 + (i/3), (i%3)*2);
                QDoubleSpinBox *sb = new QDoubleSpinBox();
                sb->setRange(-9999, 9999);
                sb->setValue(m_tempConfig.tools[toolIdx].offset[i]);
                sb->setStyleSheet("background-color: rgba(0,0,0,0.3); color: white; border: 1px solid #444;");
                connect(sb, &QDoubleSpinBox::valueChanged, [this, toolIdx, i](double v){
                    m_tempConfig.tools[toolIdx].offset[i] = v;
                });
                grid->addWidget(sb, 1 + (i/3), (i%3)*2 + 1);
            }

            // Delete Button (Except ID 0)
            if (currentId != 0) {
                QPushButton* btnDel = HexaWidgets::createButtonDanger("DELETE", nullptr, 100, 30);
                connect(btnDel, &QPushButton::clicked, [this, toolIdx, comboTools, currentId](){
                    m_tempConfig.tools.removeAt(toolIdx);
                    comboTools->setItemText(currentId, QString("ID %1: Empty Slot %1").arg(currentId));
                    emit comboTools->currentIndexChanged(currentId);
                });
                grid->addWidget(btnDel, 4, 0, 1, 4, Qt::AlignRight);
            }
        }
    };

    connect(comboTools, &QComboBox::currentIndexChanged, refreshFields);
    refreshFields();

    layout->addStretch();
    return w;
}

QWidget* SettingsOverlay::createBasesEditor() {
    QWidget *w = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(w);

    QComboBox *comboBases = HexaWidgets::createComboBox();
    for (int i=0; i<10; ++i) {
        QString name = "Empty Slot " + QString::number(i);
        for(const auto& b : m_tempConfig.bases) {
            if(b.id == i) { name = b.name; break; }
        }
        comboBases->addItem(QString("ID %1: %2").arg(i).arg(name), i);
    }
    layout->addWidget(comboBases);

    QFrame *fieldsFrame = new QFrame();
    QGridLayout *grid = new QGridLayout(fieldsFrame);
    layout->addWidget(fieldsFrame);

    auto refreshFields = [this, grid, comboBases]() {
        QLayoutItem *child;
        while ((child = grid->takeAt(0)) != nullptr) {
            delete child->widget(); delete child;
        }

        int currentId = comboBases->currentData().toInt();
        int baseIdx = -1;
        for(int i=0; i<m_tempConfig.bases.size(); ++i) {
            if (m_tempConfig.bases[i].id == currentId) { baseIdx = i; break; }
        }

        bool exists = (baseIdx != -1);

        if (!exists) {
            QPushButton* btnCreate = HexaWidgets::createButtonStd("CREATE BASE", nullptr, 200, 40);
            connect(btnCreate, &QPushButton::clicked, [this, currentId, comboBases](){
                HmiBaseData newBase;
                newBase.id = currentId;
                newBase.name = "New Base";
                newBase.offset.fill(0.0, 6);
                m_tempConfig.bases.append(newBase);
                comboBases->setItemText(currentId, QString("ID %1: New Base").arg(currentId));
                emit comboBases->currentIndexChanged(currentId);
            });
            grid->addWidget(btnCreate, 0, 0, 1, 3, Qt::AlignCenter);
        } else {
            // Name Field
            grid->addWidget(HexaWidgets::createLabelText("Name:"), 0, 0);
            QLineEdit *edName = new QLineEdit(m_tempConfig.bases[baseIdx].name);
            edName->setStyleSheet(Hexa::Styles::ComboBox);
            connect(edName, &QLineEdit::textChanged, [this, baseIdx, comboBases, currentId](const QString &v){
                m_tempConfig.bases[baseIdx].name = v;
                comboBases->setItemText(currentId, QString("ID %1: %2").arg(currentId).arg(v));
            });
            grid->addWidget(edName, 0, 1, 1, 3);

            // Offsets
            QStringList labels = {"X", "Y", "Z", "Rx", "Ry", "Rz"};
            for(int i=0; i<6; ++i) {
                grid->addWidget(HexaWidgets::createLabelText(labels[i]), 1 + (i/3), (i%3)*2);
                QDoubleSpinBox *sb = new QDoubleSpinBox();
                sb->setRange(-9999, 9999);
                sb->setValue(m_tempConfig.bases[baseIdx].offset[i]);
                sb->setStyleSheet("background-color: rgba(0,0,0,0.3); color: white; border: 1px solid #444;");
                connect(sb, &QDoubleSpinBox::valueChanged, [this, baseIdx, i](double v){
                    m_tempConfig.bases[baseIdx].offset[i] = v;
                });
                grid->addWidget(sb, 1 + (i/3), (i%3)*2 + 1);
            }

            if (currentId != 0) {
                QPushButton* btnDel = HexaWidgets::createButtonDanger("DELETE", nullptr, 100, 30);
                connect(btnDel, &QPushButton::clicked, [this, baseIdx, comboBases, currentId](){
                    m_tempConfig.bases.removeAt(baseIdx);
                    comboBases->setItemText(currentId, QString("ID %1: Empty Slot %1").arg(currentId));
                    emit comboBases->currentIndexChanged(currentId);
                });
                grid->addWidget(btnDel, 4, 0, 1, 4, Qt::AlignRight);
            }
        }
    };

    connect(comboBases, &QComboBox::currentIndexChanged, refreshFields);
    refreshFields();

    layout->addStretch();
    return w;
}

void SettingsOverlay::onApplyClicked()
{
    RobotService::instance()->applySettings(m_tempConfig);
    emit applyRequested(m_tempConfig);
    emit closeRequested();
}

void SettingsOverlay::onCancelClicked()
{
    emit closeRequested();
}

void SettingsOverlay::mousePressEvent(QMouseEvent *event)
{
    event->accept();
}

void SettingsOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 200));
}

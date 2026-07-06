// --- START OF FILE: HexaStudio/jog_control/FramePickerDialog.cpp ---
#include "FramePickerDialog.h"

#include "HexaTheme.h"
#include "HexaWidgets.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

namespace hexa {

namespace {
// Matches the pendant column the panel lives in; the picker must not look like a foreign window.
constexpr int kDialogWidthPx = 300;
constexpr int kListMinHeightPx = 160;
constexpr int kOffsetLabelWidthPx = 28;
} // namespace

FramePickerDialog::FramePickerDialog(QWidget* parent) : QDialog(parent) {
    setModal(true);
    // Frameless + application background: an OS-decorated window breaks the pendant look. CANCEL /
    // SELECT are the only exits, which is the intended modal flow.
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral("QDialog { background-color: %1; border: 1px solid %2; }")
                      .arg(Hexa::Colors::Background, Hexa::Colors::Border));
    setupUi();
    setFixedWidth(kDialogWidthPx);
}

void FramePickerDialog::setupUi() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);

    // Section title, same style language as the panel's "MONITOR SYSTEM" heading.
    m_title = new QLabel(QStringLiteral("SELECT TOOL"), this);
    m_title->setStyleSheet(Hexa::Styles::LabelHeaderSimple);
    m_title->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_title);
    layout->addWidget(HexaWidgets::createSeparatorH());

    // Flat list of frames: single click selects + previews, double click confirms.
    m_list = new QListWidget(this);
    m_list->setStyleSheet(Hexa::Styles::ListView + QStringLiteral(
        "QListView::item { padding: 8px; color: %1; font-family: '%2'; font-size: 12px; }")
        .arg(Hexa::Colors::TextMain, Hexa::Fonts::familyUI()));
    m_list->setMinimumHeight(kListMinHeightPx);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_list, &QListWidget::currentRowChanged, this, &FramePickerDialog::onCurrentRowChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this]() { accept(); });
    layout->addWidget(m_list, 1);

    // Offset preview card: translations (mm) left, rotations (deg) right, mono values right-aligned.
    // Same instrument-card look as the panel's monitor section.
    QFrame* card = HexaWidgets::createMainPanel();
    QGridLayout* grid = new QGridLayout(card);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setVerticalSpacing(6);
    grid->setHorizontalSpacing(8);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(4, 1);
    grid->setColumnMinimumWidth(2, 12); // breathing room between the two halves
    const QStringList offsetNames = {"X:", "Y:", "Z:", "Rx:", "Ry:", "Rz:"};
    for (int i = 0; i < offsetNames.size(); ++i) {
        QLabel* label = HexaWidgets::createLabelText(offsetNames[i]);
        label->setFixedWidth(kOffsetLabelWidthPx);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        QLabel* value = HexaWidgets::createLabelData("---");
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_offsetValues.append(value);

        // Column layout: X/Y/Z in the left half (rows 0..2), Rx/Ry/Rz in the right half.
        const int row = i % 3;
        const int col = (i < 3) ? 0 : 3;
        grid->addWidget(label, row, col);
        grid->addWidget(value, row, col + 1);
    }
    layout->addWidget(card);

    QHBoxLayout* buttons = new QHBoxLayout();
    buttons->setSpacing(8);
    QPushButton* btnCancel = HexaWidgets::createButtonStd("CANCEL", this, 0, 36);
    btnCancel->setMaximumWidth(QWIDGETSIZE_MAX);
    btnCancel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(btnCancel, 1);

    QPushButton* btnSelect = HexaWidgets::createButtonStd("SELECT", this, 0, 36);
    btnSelect->setMaximumWidth(QWIDGETSIZE_MAX);
    btnSelect->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Primary-accent border: SELECT is the main action of the picker.
    btnSelect->setStyleSheet(Hexa::Styles::ButtonBase +
        QStringLiteral("QPushButton { border: 1px solid %1; color: white; }").arg(Hexa::Colors::Primary) +
        Hexa::Styles::ButtonInteract);
    connect(btnSelect, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(btnSelect, 1);
    layout->addLayout(buttons);
}

void FramePickerDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (parentWidget() != nullptr) {
        const QRect hostRect = parentWidget()->window()->geometry();
        move(hostRect.center() - rect().center());
    }
}

void FramePickerDialog::setToolData(const QVector<HmiToolData>& tools, int currentId) {
    m_tools = tools;
    m_bases.clear();
    m_isToolMode = true;
    m_selectedId = currentId;
    m_title->setText(QStringLiteral("SELECT TOOL"));

    QStringList names;
    QVector<int> ids;
    for (const HmiToolData& tool : tools) {
        names.append(tool.name);
        ids.append(tool.id);
    }
    populateList(names, ids, currentId);
}

void FramePickerDialog::setBaseData(const QVector<HmiBaseData>& bases, int currentId) {
    m_bases = bases;
    m_tools.clear();
    m_isToolMode = false;
    m_selectedId = currentId;
    m_title->setText(QStringLiteral("SELECT BASE"));

    QStringList names;
    QVector<int> ids;
    for (const HmiBaseData& base : bases) {
        names.append(base.name);
        ids.append(base.id);
    }
    populateList(names, ids, currentId);
}

int FramePickerDialog::getSelectedToolId() const {
    if (!m_isToolMode) return -1;
    return m_selectedId;
}

int FramePickerDialog::getSelectedBaseId() const {
    if (m_isToolMode) return -1;
    return m_selectedId;
}

void FramePickerDialog::populateList(const QStringList& names, const QVector<int>& ids,
                                     int currentId) {
    m_list->clear();
    int currentRow = 0;
    for (int i = 0; i < names.size(); ++i) {
        QListWidgetItem* item = new QListWidgetItem(names[i], m_list);
        item->setData(Qt::UserRole, ids[i]);
        if (ids[i] == currentId) currentRow = i;
    }
    if (m_list->count() > 0) {
        m_list->setCurrentRow(currentRow); // fires onCurrentRowChanged -> preview + m_selectedId
    } else {
        clearOffset(); // empty config: nothing selectable, preview shows placeholders
    }
}

void FramePickerDialog::onCurrentRowChanged(int row) {
    if (row < 0) {
        clearOffset();
        return;
    }
    if (m_isToolMode) {
        if (row < m_tools.size()) {
            m_selectedId = m_tools[row].id;
            showOffset(m_tools[row].offset);
        }
    } else {
        if (row < m_bases.size()) {
            m_selectedId = m_bases[row].id;
            showOffset(m_bases[row].offset);
        }
    }
}

void FramePickerDialog::showOffset(const QVector<double>& offset) {
    for (int i = 0; i < m_offsetValues.size(); ++i) {
        const double value = (i < offset.size()) ? offset[i] : 0.0;
        m_offsetValues[i]->setText(QString::number(value, 'f', 2));
    }
}

void FramePickerDialog::clearOffset() {
    for (QLabel* value : m_offsetValues) value->setText(QStringLiteral("---"));
}

} // namespace hexa
// --- END OF FILE: HexaStudio/jog_control/FramePickerDialog.cpp ---

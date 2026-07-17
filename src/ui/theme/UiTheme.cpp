#include "ui/theme/UiTheme.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QString>

namespace {

constexpr char kBackgroundBase[] = "#070A0D";
constexpr char kBackgroundSurface[] = "#0D1217";
constexpr char kBackgroundRaised[] = "#131A21";
constexpr char kBackgroundHover[] = "#18212A";
constexpr char kBorderDim[] = "#202A33";
constexpr char kBorderBright[] = "#34424F";
constexpr char kTextPrimary[] = "#E6EDF3";
constexpr char kTextSecondary[] = "#98A6B3";
constexpr char kTextMuted[] = "#64727F";
constexpr char kAccent[] = "#F59E0B";
constexpr char kTechnical[] = "#22D3EE";
constexpr char kPositive[] = "#22C55E";
constexpr char kNegative[] = "#EF4444";

void ReplaceToken(QString& styleSheet, const QString& token, const char* value) {
    styleSheet.replace(token, QString::fromLatin1(value));
}

QColor ThemeColor(const char* value) {
    return QColor(QString::fromLatin1(value));
}

QPalette BuildDarkPalette() {
    QPalette palette;
    palette.setColor(QPalette::Window, ThemeColor(kBackgroundBase));
    palette.setColor(QPalette::WindowText, ThemeColor(kTextPrimary));
    palette.setColor(QPalette::Base, ThemeColor(kBackgroundBase));
    palette.setColor(QPalette::AlternateBase, ThemeColor(kBackgroundSurface));
    palette.setColor(QPalette::ToolTipBase, ThemeColor(kBackgroundRaised));
    palette.setColor(QPalette::ToolTipText, ThemeColor(kTextPrimary));
    palette.setColor(QPalette::Text, ThemeColor(kTextPrimary));
    palette.setColor(QPalette::Button, ThemeColor(kBackgroundRaised));
    palette.setColor(QPalette::ButtonText, ThemeColor(kTextSecondary));
    palette.setColor(QPalette::BrightText, ThemeColor(kNegative));
    palette.setColor(QPalette::Highlight, ThemeColor(kAccent));
    palette.setColor(QPalette::HighlightedText, ThemeColor(kBackgroundBase));
    palette.setColor(QPalette::Link, ThemeColor(kTechnical));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, ThemeColor(kTextMuted));
    palette.setColor(QPalette::Disabled, QPalette::Text, ThemeColor(kTextMuted));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, ThemeColor(kTextMuted));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, ThemeColor(kBorderDim));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, ThemeColor(kTextMuted));
    return palette;
}

QString BuildDarkStyleSheet() {
    QString styleSheet = QStringLiteral(R"QSS(
QMainWindow,
QDialog {
    background-color: {{BACKGROUND_BASE}};
    color: {{TEXT_PRIMARY}};
}

QWidget#centralwidget,
QWidget#widget_videoArea {
    background-color: {{BACKGROUND_BASE}};
}

QWidget#widget_header {
    background-color: {{BACKGROUND_SURFACE}};
    border-bottom: 2px solid {{ACCENT}};
}

QWidget#widget_commandBar {
    background-color: {{BACKGROUND_RAISED}};
    border-bottom: 1px solid {{BORDER_DIM}};
}

QWidget#widget_sourcePanel,
QWidget#widget_inspectorPanel {
    background-color: {{BACKGROUND_SURFACE}};
}

QWidget#widget_sourcePanel {
    border-right: 1px solid {{BORDER_DIM}};
}

QWidget#widget_inspectorPanel {
    border-left: 1px solid {{BORDER_DIM}};
}

QWidget#widget_centerPanel {
    background-color: {{BACKGROUND_BASE}};
}

QWidget#widget_viewportHeader {
    background-color: {{BACKGROUND_SURFACE}};
    border: 1px solid {{BORDER_DIM}};
}

QWidget#widget_videoArea {
    border: 1px solid {{BORDER_BRIGHT}};
}

QWidget#widget_func {
    background-color: {{BACKGROUND_SURFACE}};
    border-top: 1px solid {{BORDER_DIM}};
}

QLabel,
QCheckBox,
QRadioButton {
    color: {{TEXT_SECONDARY}};
    background-color: transparent;
}

QLabel#label_productMark {
    padding: 2px 7px;
    background-color: {{ACCENT}};
    color: {{BACKGROUND_BASE}};
    border-radius: 2px;
    font-weight: 700;
}

QLabel#label_productName {
    color: {{TEXT_PRIMARY}};
    font-size: 15px;
    font-weight: 700;
}

QLabel#label_commandTitle,
QLabel#label_mediaLibrary,
QLabel#label_streams,
QLabel#label_inspectorTitle {
    color: {{ACCENT}};
    font-weight: 600;
}

QLabel#label_viewportTitle {
    color: {{TECHNICAL}};
    font-weight: 600;
}

QLabel#label_productSubtitle,
QLabel#label_workspaceMode,
QLabel#label_mediaHint,
QLabel#label_inspectorHint,
QLabel#label_viewportHint {
    color: {{TEXT_MUTED}};
}

QFrame#line_header,
QFrame#line_commandPrimary,
QFrame#line_sourcePanel,
QFrame#line_transportPrimary,
QFrame#line_transportSecondary {
    color: {{BORDER_DIM}};
}

QMenuBar {
    background-color: {{BACKGROUND_SURFACE}};
    color: {{TEXT_SECONDARY}};
    border-bottom: 1px solid {{BORDER_DIM}};
    spacing: 2px;
}

QMenuBar::item {
    padding: 5px 10px;
    background-color: transparent;
}

QMenuBar::item:selected,
QMenuBar::item:pressed {
    background-color: {{BACKGROUND_HOVER}};
    color: {{TEXT_PRIMARY}};
}

QMenu {
    padding: 4px;
    background-color: {{BACKGROUND_SURFACE}};
    color: {{TEXT_PRIMARY}};
    border: 1px solid {{BORDER_BRIGHT}};
}

QMenu::item {
    padding: 5px 24px 5px 10px;
}

QMenu::item:selected {
    background-color: {{BACKGROUND_HOVER}};
    color: {{ACCENT}};
}

QMenu::separator {
    height: 1px;
    margin: 4px 8px;
    background-color: {{BORDER_DIM}};
}

QStatusBar {
    background-color: {{BACKGROUND_SURFACE}};
    color: {{TEXT_SECONDARY}};
    border-top: 1px solid {{BORDER_DIM}};
}

QStatusBar::item {
    border: none;
}

QToolBar {
    padding: 3px;
    spacing: 4px;
    background-color: {{BACKGROUND_SURFACE}};
    border: none;
    border-bottom: 1px solid {{BORDER_DIM}};
}

QPushButton,
QToolButton {
    min-height: 26px;
    padding: 3px 10px;
    background-color: {{BACKGROUND_RAISED}};
    color: {{TEXT_SECONDARY}};
    border: 1px solid {{BORDER_DIM}};
    border-radius: 2px;
}

QPushButton:hover,
QToolButton:hover {
    background-color: {{BACKGROUND_HOVER}};
    color: {{TEXT_PRIMARY}};
    border-color: {{BORDER_BRIGHT}};
}

QPushButton:pressed,
QToolButton:pressed,
QPushButton:checked,
QToolButton:checked {
    background-color: {{ACCENT}};
    color: {{BACKGROUND_BASE}};
    border-color: {{ACCENT}};
}

QPushButton:focus,
QToolButton:focus {
    border-color: {{ACCENT}};
}

QPushButton:disabled,
QToolButton:disabled {
    background-color: {{BACKGROUND_SURFACE}};
    color: {{TEXT_MUTED}};
    border-color: {{BORDER_DIM}};
}

QPushButton#pushButton_play,
QPushButton#pushButton_startAll {
    color: {{POSITIVE}};
    border-color: {{POSITIVE}};
}

QPushButton#pushButton_removeStream,
QPushButton#pushButton_stopAll {
    color: {{NEGATIVE}};
    border-color: {{NEGATIVE}};
}

QPushButton#pushButton_fullscreen {
    color: {{ACCENT}};
    border-color: {{ACCENT}};
}

QLineEdit,
QTextEdit,
QPlainTextEdit,
QComboBox,
QSpinBox,
QDoubleSpinBox,
QDateEdit,
QTimeEdit,
QDateTimeEdit {
    min-height: 26px;
    padding: 2px 7px;
    background-color: {{BACKGROUND_RAISED}};
    color: {{TEXT_PRIMARY}};
    border: 1px solid {{BORDER_DIM}};
    border-radius: 2px;
}

QLineEdit:hover,
QTextEdit:hover,
QPlainTextEdit:hover,
QComboBox:hover,
QSpinBox:hover,
QDoubleSpinBox:hover,
QDateEdit:hover,
QTimeEdit:hover,
QDateTimeEdit:hover {
    border-color: {{BORDER_BRIGHT}};
}

QLineEdit:focus,
QTextEdit:focus,
QPlainTextEdit:focus,
QComboBox:focus,
QSpinBox:focus,
QDoubleSpinBox:focus,
QDateEdit:focus,
QTimeEdit:focus,
QDateTimeEdit:focus {
    border-color: {{ACCENT}};
}

QLineEdit:disabled,
QTextEdit:disabled,
QPlainTextEdit:disabled,
QComboBox:disabled,
QSpinBox:disabled,
QDoubleSpinBox:disabled,
QDateEdit:disabled,
QTimeEdit:disabled,
QDateTimeEdit:disabled {
    background-color: {{BACKGROUND_SURFACE}};
    color: {{TEXT_MUTED}};
}

QComboBox::drop-down {
    width: 22px;
    background-color: {{BACKGROUND_SURFACE}};
    border: none;
    border-left: 1px solid {{BORDER_DIM}};
}

QComboBox QAbstractItemView {
    background-color: {{BACKGROUND_SURFACE}};
    color: {{TEXT_PRIMARY}};
    border: 1px solid {{BORDER_BRIGHT}};
    outline: none;
    selection-background-color: {{BACKGROUND_HOVER}};
    selection-color: {{ACCENT}};
}

QAbstractSpinBox::up-button,
QAbstractSpinBox::down-button {
    width: 18px;
    background-color: {{BACKGROUND_SURFACE}};
    border: none;
    border-left: 1px solid {{BORDER_DIM}};
}

QAbstractSpinBox::up-button:hover,
QAbstractSpinBox::down-button:hover {
    background-color: {{BACKGROUND_HOVER}};
}

QListView,
QTreeView,
QTableView {
    background-color: {{BACKGROUND_BASE}};
    alternate-background-color: {{BACKGROUND_SURFACE}};
    color: {{TEXT_SECONDARY}};
    border: 1px solid {{BORDER_DIM}};
    outline: none;
    gridline-color: {{BORDER_DIM}};
}

QListView::item,
QTreeView::item,
QTableView::item {
    min-height: 26px;
    padding: 3px 6px;
    border-bottom: 1px solid {{BORDER_DIM}};
}

QListView::item:hover,
QTreeView::item:hover,
QTableView::item:hover {
    background-color: {{BACKGROUND_HOVER}};
    color: {{TEXT_PRIMARY}};
}

QListView::item:selected,
QTreeView::item:selected,
QTableView::item:selected {
    background-color: {{BACKGROUND_RAISED}};
    color: {{ACCENT}};
    border-left: 2px solid {{ACCENT}};
}

QHeaderView::section {
    padding: 4px 6px;
    background-color: {{BACKGROUND_SURFACE}};
    color: {{TEXT_SECONDARY}};
    border: none;
    border-right: 1px solid {{BORDER_DIM}};
    border-bottom: 1px solid {{BORDER_DIM}};
}

QTabWidget::pane {
    background-color: {{BACKGROUND_SURFACE}};
    border: 1px solid {{BORDER_DIM}};
}

QTabBar::tab {
    min-height: 24px;
    padding: 4px 10px;
    background-color: {{BACKGROUND_SURFACE}};
    color: {{TEXT_MUTED}};
    border: none;
    border-bottom: 2px solid transparent;
}

QTabBar::tab:hover {
    color: {{TEXT_PRIMARY}};
    background-color: {{BACKGROUND_HOVER}};
}

QTabBar::tab:selected {
    color: {{ACCENT}};
    border-bottom-color: {{ACCENT}};
}

QGroupBox {
    margin-top: 12px;
    padding-top: 8px;
    background-color: {{BACKGROUND_SURFACE}};
    border: 1px solid {{BORDER_DIM}};
    border-radius: 2px;
    font-weight: bold;
}

QGroupBox::title {
    subcontrol-origin: margin;
    left: 8px;
    padding: 0 4px;
    color: {{ACCENT}};
    background-color: {{BACKGROUND_SURFACE}};
}

QCheckBox,
QRadioButton {
    spacing: 6px;
}

QCheckBox:hover,
QRadioButton:hover {
    color: {{TEXT_PRIMARY}};
}

QCheckBox:disabled,
QRadioButton:disabled {
    color: {{TEXT_MUTED}};
}

QCheckBox::indicator,
QRadioButton::indicator {
    width: 14px;
    height: 14px;
    background-color: {{BACKGROUND_RAISED}};
    border: 1px solid {{BORDER_BRIGHT}};
}

QCheckBox::indicator {
    border-radius: 2px;
}

QRadioButton::indicator {
    border-radius: 7px;
}

QCheckBox::indicator:checked,
QRadioButton::indicator:checked {
    background-color: {{ACCENT}};
    border: 3px solid {{BACKGROUND_RAISED}};
}

QCheckBox::indicator:disabled,
QRadioButton::indicator:disabled {
    background-color: {{BACKGROUND_SURFACE}};
    border-color: {{BORDER_DIM}};
}

QSlider::groove:horizontal {
    height: 4px;
    background-color: {{BORDER_DIM}};
    border-radius: 2px;
}

QSlider::sub-page:horizontal {
    background-color: {{ACCENT}};
    border-radius: 2px;
}

QSlider::add-page:horizontal {
    background-color: {{BORDER_DIM}};
    border-radius: 2px;
}

QSlider::handle:horizontal {
    width: 14px;
    margin: -5px 0;
    background-color: {{ACCENT}};
    border: 1px solid {{BACKGROUND_BASE}};
    border-radius: 7px;
}

QSlider::handle:horizontal:hover {
    background-color: {{TEXT_PRIMARY}};
    border-color: {{ACCENT}};
}

QSlider::groove:horizontal:disabled,
QSlider::sub-page:horizontal:disabled,
QSlider::add-page:horizontal:disabled {
    background-color: {{BACKGROUND_RAISED}};
}

QSlider::handle:horizontal:disabled {
    background-color: {{TEXT_MUTED}};
}

QProgressBar {
    min-height: 8px;
    background-color: {{BACKGROUND_RAISED}};
    color: {{TEXT_PRIMARY}};
    border: 1px solid {{BORDER_DIM}};
    text-align: center;
}

QProgressBar::chunk {
    background-color: {{ACCENT}};
}

QScrollBar:vertical {
    width: 8px;
    margin: 0;
    background-color: {{BACKGROUND_BASE}};
}

QScrollBar:horizontal {
    height: 8px;
    margin: 0;
    background-color: {{BACKGROUND_BASE}};
}

QScrollBar::handle:vertical,
QScrollBar::handle:horizontal {
    min-width: 24px;
    min-height: 24px;
    background-color: {{BORDER_BRIGHT}};
    border-radius: 4px;
}

QScrollBar::handle:vertical:hover,
QScrollBar::handle:horizontal:hover {
    background-color: {{TEXT_MUTED}};
}

QScrollBar::add-line,
QScrollBar::sub-line,
QScrollBar::add-page,
QScrollBar::sub-page {
    width: 0;
    height: 0;
    background-color: transparent;
}

QSplitter::handle {
    background-color: {{BORDER_DIM}};
}

QSplitter::handle:hover {
    background-color: {{ACCENT}};
}

QToolTip {
    padding: 4px 7px;
    background-color: {{BACKGROUND_RAISED}};
    color: {{TEXT_PRIMARY}};
    border: 1px solid {{BORDER_BRIGHT}};
}

QLabel#label_current,
QLabel#label_duration {
    color: {{TECHNICAL}};
}
)QSS");

    ReplaceToken(styleSheet, QStringLiteral("{{BACKGROUND_BASE}}"), kBackgroundBase);
    ReplaceToken(styleSheet, QStringLiteral("{{BACKGROUND_SURFACE}}"), kBackgroundSurface);
    ReplaceToken(styleSheet, QStringLiteral("{{BACKGROUND_RAISED}}"), kBackgroundRaised);
    ReplaceToken(styleSheet, QStringLiteral("{{BACKGROUND_HOVER}}"), kBackgroundHover);
    ReplaceToken(styleSheet, QStringLiteral("{{BORDER_DIM}}"), kBorderDim);
    ReplaceToken(styleSheet, QStringLiteral("{{BORDER_BRIGHT}}"), kBorderBright);
    ReplaceToken(styleSheet, QStringLiteral("{{TEXT_PRIMARY}}"), kTextPrimary);
    ReplaceToken(styleSheet, QStringLiteral("{{TEXT_SECONDARY}}"), kTextSecondary);
    ReplaceToken(styleSheet, QStringLiteral("{{TEXT_MUTED}}"), kTextMuted);
    ReplaceToken(styleSheet, QStringLiteral("{{ACCENT}}"), kAccent);
    ReplaceToken(styleSheet, QStringLiteral("{{TECHNICAL}}"), kTechnical);
    ReplaceToken(styleSheet, QStringLiteral("{{POSITIVE}}"), kPositive);
    ReplaceToken(styleSheet, QStringLiteral("{{NEGATIVE}}"), kNegative);

    return styleSheet;
}

}  // namespace

void UiTheme::Apply(QApplication& application) {
    application.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    application.setPalette(BuildDarkPalette());
    application.setStyleSheet(BuildDarkStyleSheet());
}

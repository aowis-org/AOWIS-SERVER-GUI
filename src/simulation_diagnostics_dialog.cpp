#include "simulation_diagnostics_dialog.h"

#include "hydraulic_data.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QHeaderView>
#include <QMetaEnum>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace
{
QString severityText(HydraulicSimulationDiagnosticSeverity severity)
{
    switch (severity)
    {
    case HydraulicSimulationDiagnosticSeverity::Information:
        return QStringLiteral("Info");
    case HydraulicSimulationDiagnosticSeverity::Warning:
        return QStringLiteral("Warning");
    case HydraulicSimulationDiagnosticSeverity::Error:
        return QStringLiteral("Error");
    case HydraulicSimulationDiagnosticSeverity::Fatal:
        return QStringLiteral("Fatal");
    }

    return QStringLiteral("Unknown");
}

QColor severityColor(HydraulicSimulationDiagnosticSeverity severity)
{
    switch (severity)
    {
    case HydraulicSimulationDiagnosticSeverity::Warning:
        return QColor(190, 110, 0);
    case HydraulicSimulationDiagnosticSeverity::Error:
    case HydraulicSimulationDiagnosticSeverity::Fatal:
        return QColor(210, 0, 0);
    default:
        return QColor();
    }
}

QString enumText(const char *enumerator_name, int value)
{
    const QMetaObject &meta_object = HydraulicSimulationStatusEnums::staticMetaObject;
    const int enumerator_index = meta_object.indexOfEnumerator(enumerator_name);
    if (enumerator_index < 0)
        return QStringLiteral("None");

    const QMetaEnum meta_enum = meta_object.enumerator(enumerator_index);
    const char *key = meta_enum.valueToKey(value);
    return key == nullptr ? QStringLiteral("None") : QString::fromLatin1(key);
}

QString enumText(HydraulicSimulationStatusStage value)
{
    return enumText("Stage", static_cast<int>(value));
}

QString enumText(HydraulicSimulationStatusOperation value)
{
    return enumText("Operation", static_cast<int>(value));
}

QString enumText(HydraulicSimulationStatusProperty value)
{
    return enumText("Property", static_cast<int>(value));
}

QString enumText(HydraulicSimulationStatusEntityType value)
{
    return enumText("EntityType", static_cast<int>(value));
}

QString entityText(const HydraulicSimulationDiagnostic &diagnostic)
{
    QString text = enumText(diagnostic.entity.type);
    if (!diagnostic.entity.id.isEmpty())
        text += QStringLiteral(" ") + diagnostic.entity.id;
    return text;
}
}

SimulationDiagnosticsDialog::SimulationDiagnosticsDialog(HydraulicData *hydraulic_data, QWidget *parent)
    : QDialog(parent),
      hydraulic_data(hydraulic_data)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Simulation Diagnostics"));
    setModal(false);
    resize(900, 600);

    this->table_diagnostics = new QTableWidget(this);
    this->table_diagnostics->setColumnCount(3);
    this->table_diagnostics->setHorizontalHeaderLabels({tr("Severity"), tr("Object"), tr("Message")});
    this->table_diagnostics->setSelectionBehavior(QAbstractItemView::SelectRows);
    this->table_diagnostics->setSelectionMode(QAbstractItemView::SingleSelection);
    this->table_diagnostics->setEditTriggers(QAbstractItemView::NoEditTriggers);
    this->table_diagnostics->verticalHeader()->hide();
    this->table_diagnostics->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    this->table_diagnostics->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    this->table_diagnostics->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    this->text_details = new QTextBrowser(this);
    this->text_details->setOpenExternalLinks(false);

    QSplitter *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(this->table_diagnostics);
    splitter->addWidget(this->text_details);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(splitter);

    connect(this->table_diagnostics, &QTableWidget::currentCellChanged, this,
        [this](int current_row, int, int, int)
    {
        if (current_row < 0)
        {
            this->text_details->clear();
            return;
        }

        QTableWidgetItem *item = this->table_diagnostics->item(current_row, 0);
        if (item == nullptr)
            return;
        showDiagnosticDetails(item->data(Qt::UserRole).toInt());
    });

    if (this->hydraulic_data != nullptr)
    {
        connect(this->hydraulic_data, &HydraulicData::signalSimulationResultTimelineChanged,
            this, [this](bool)
        {
            refresh();
        });
    }

    refresh();
}

void SimulationDiagnosticsDialog::refresh()
{
    this->table_diagnostics->setRowCount(0);
    this->text_details->clear();
    if (this->hydraulic_data == nullptr)
        return;

    const std::optional<HydraulicSimulationResultTimeline> &result_timeline = this->hydraulic_data->simulationResultTimeline();
    if (!result_timeline.has_value())
        return;

    const QList<HydraulicSimulationDiagnostic> &diagnostics = result_timeline->diagnostics;
    this->table_diagnostics->setRowCount(diagnostics.size());
    for (qsizetype index = 0; index < diagnostics.size(); ++index)
    {
        const HydraulicSimulationDiagnostic &diagnostic = diagnostics.at(index);
        QTableWidgetItem *severity_item = new QTableWidgetItem(severityText(diagnostic.severity));
        severity_item->setData(Qt::UserRole, static_cast<int>(index));
        const QColor color = severityColor(diagnostic.severity);
        if (color.isValid())
            severity_item->setForeground(QBrush(color));

        QTableWidgetItem *entity_item = new QTableWidgetItem(entityText(diagnostic));
        QTableWidgetItem *message_item = new QTableWidgetItem(diagnostic.message);
        this->table_diagnostics->setItem(static_cast<int>(index), 0, severity_item);
        this->table_diagnostics->setItem(static_cast<int>(index), 1, entity_item);
        this->table_diagnostics->setItem(static_cast<int>(index), 2, message_item);
    }

    if (!diagnostics.isEmpty())
    {
        this->table_diagnostics->selectRow(0);
        showDiagnosticDetails(0);
    }
}

void SimulationDiagnosticsDialog::showDiagnosticDetails(int diagnostic_index)
{
    if (this->hydraulic_data == nullptr)
        return;

    const std::optional<HydraulicSimulationResultTimeline> &result_timeline = this->hydraulic_data->simulationResultTimeline();
    if (!result_timeline.has_value())
        return;

    const QList<HydraulicSimulationDiagnostic> &diagnostics = result_timeline->diagnostics;
    if (diagnostic_index < 0 || diagnostic_index >= diagnostics.size())
        return;

    const HydraulicSimulationDiagnostic &diagnostic = diagnostics.at(diagnostic_index);
    QString html;
    html += QStringLiteral("<h3>%1</h3>").arg(diagnostic.message.toHtmlEscaped());
    html += QStringLiteral("<table cellspacing=\"5\">");
    html += QStringLiteral("<tr><td><b>Severity</b></td><td>%1</td></tr>").arg(severityText(diagnostic.severity).toHtmlEscaped());
    html += QStringLiteral("<tr><td><b>Stage</b></td><td>%1</td></tr>").arg(enumText(diagnostic.stage).toHtmlEscaped());
    html += QStringLiteral("<tr><td><b>Operation</b></td><td>%1</td></tr>").arg(enumText(diagnostic.operation).toHtmlEscaped());
    html += QStringLiteral("<tr><td><b>Property</b></td><td>%1</td></tr>").arg(enumText(diagnostic.property).toHtmlEscaped());
    html += QStringLiteral("<tr><td><b>Object type</b></td><td>%1</td></tr>").arg(enumText(diagnostic.entity.type).toHtmlEscaped());
    html += QStringLiteral("<tr><td><b>Object ID</b></td><td>%1</td></tr>").arg(diagnostic.entity.id.toHtmlEscaped());
    html += QStringLiteral("<tr><td><b>Object UUID</b></td><td>%1</td></tr>").arg(diagnostic.entity.uuid.toString(QUuid::WithoutBraces).toHtmlEscaped());
    html += QStringLiteral("<tr><td><b>Backend</b></td><td>%1</td></tr>").arg(diagnostic.backend_name.toHtmlEscaped());
    html += QStringLiteral("<tr><td><b>Backend operation</b></td><td>%1</td></tr>").arg(diagnostic.backend_operation.toHtmlEscaped());
    html += QStringLiteral("<tr><td><b>Backend code</b></td><td>%1</td></tr>").arg(diagnostic.backend_error_code);
    html += QStringLiteral("</table>");

    if (!diagnostic.message_backend.isEmpty())
        html += QStringLiteral("<p><b>Backend message</b><br>%1</p>").arg(diagnostic.message_backend.toHtmlEscaped());

    if (!diagnostic.details.isEmpty())
    {
        html += QStringLiteral("<p><b>Details</b><ul>");
        for (const QString &detail : diagnostic.details)
            html += QStringLiteral("<li>%1</li>").arg(detail.toHtmlEscaped());
        html += QStringLiteral("</ul></p>");
    }

    this->text_details->setHtml(html);
}

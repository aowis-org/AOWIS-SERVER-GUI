#include "simulation_diagnostics_dialog.h"

#include "hydraulic_data.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QFontMetrics>
#include <QLabel>
#include <QMetaEnum>
#include <QSizePolicy>
#include <QSplitter>
#include <QListWidget>
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

QString resultValidityText(HydraulicSimulationResultValidity validity)
{
    switch (validity)
    {
    case HydraulicSimulationResultValidity::Valid:
        return QStringLiteral("Valid");
    case HydraulicSimulationResultValidity::Partial:
        return QStringLiteral("Partial");
    case HydraulicSimulationResultValidity::Invalid:
        return QStringLiteral("Invalid");
    }

    return QStringLiteral("Invalid");
}

QString resultValidityColor(HydraulicSimulationResultValidity validity)
{
    switch (validity)
    {
    case HydraulicSimulationResultValidity::Valid:
        return QStringLiteral("#15803d");
    case HydraulicSimulationResultValidity::Partial:
        return QStringLiteral("#c77800");
    case HydraulicSimulationResultValidity::Invalid:
        return QStringLiteral("#d00000");
    }

    return QStringLiteral("#d00000");
}

QString entityText(const HydraulicSimulationDiagnostic &diagnostic)
{
    QString text = enumText(diagnostic.entity.type);
    if (!diagnostic.entity.id.isEmpty())
        text += QStringLiteral(" ") + diagnostic.entity.id;
    return text;
}

InfrastructureEntity diagnosticInfrastructureEntity(
    const HydraulicSimulationDiagnostic &diagnostic, const HydraulicData &hydraulic_data)
{
    const QUuid uuid = diagnostic.entity.uuid;
    if (uuid.isNull())
        return InfrastructureEntity::Unknown;

    switch (diagnostic.entity.type)
    {
    case HydraulicSimulationStatusEntityType::Junction:
        return InfrastructureEntity::Junction;
    case HydraulicSimulationStatusEntityType::Reservoir:
        return InfrastructureEntity::Reservoir;
    case HydraulicSimulationStatusEntityType::Tank:
        return InfrastructureEntity::Tank;
    case HydraulicSimulationStatusEntityType::Pipe:
        return InfrastructureEntity::Pipe;
    case HydraulicSimulationStatusEntityType::Pump:
        return InfrastructureEntity::Pump;
    case HydraulicSimulationStatusEntityType::Valve:
        return InfrastructureEntity::Valve;
    case HydraulicSimulationStatusEntityType::Node:
        if (hydraulic_data.junction(uuid).has_value())
            return InfrastructureEntity::Junction;
        if (hydraulic_data.reservoir(uuid).has_value())
            return InfrastructureEntity::Reservoir;
        if (hydraulic_data.tank(uuid).has_value())
            return InfrastructureEntity::Tank;
        break;
    case HydraulicSimulationStatusEntityType::Link:
        if (hydraulic_data.pipe(uuid).has_value())
            return InfrastructureEntity::Pipe;
        if (hydraulic_data.pump(uuid).has_value())
            return InfrastructureEntity::Pump;
        if (hydraulic_data.valve(uuid).has_value())
            return InfrastructureEntity::Valve;
        break;
    default:
        break;
    }

    return InfrastructureEntity::Unknown;
}
}

SimulationDiagnosticsDialog::SimulationDiagnosticsDialog(HydraulicData *hydraulic_data, QWidget *parent)
    : QDialog(parent),
      hydraulic_data(hydraulic_data)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Simulation Diagnostics"));
    setModal(false);
    resize(720, 600);
    setMinimumSize(620, 460);

    this->label_result_validity = new QLabel(this);
    this->label_result_validity->setTextFormat(Qt::RichText);
    this->label_result_validity->setWordWrap(false);
    this->label_result_validity->setTextInteractionFlags(Qt::TextSelectableByMouse);
    this->label_result_validity->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    this->label_result_validity->setMaximumHeight(this->label_result_validity->fontMetrics().lineSpacing() * 2 + 12);

    this->list_diagnostics = new QListWidget(this);
    this->list_diagnostics->setSelectionMode(QAbstractItemView::SingleSelection);
    this->list_diagnostics->setWordWrap(true);
    this->list_diagnostics->setTextElideMode(Qt::ElideNone);
    this->list_diagnostics->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->list_diagnostics->setSpacing(3);
    this->list_diagnostics->setMinimumWidth(220);
    this->list_diagnostics->setMaximumWidth(300);

    this->text_details = new QTextBrowser(this);
    this->text_details->setOpenExternalLinks(false);
    this->text_details->setMinimumWidth(340);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(this->list_diagnostics);
    splitter->addWidget(this->text_details);
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({250, 430});

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(this->label_result_validity);
    layout->addWidget(splitter);

    connect(this->list_diagnostics, &QListWidget::currentItemChanged, this,
        [this](QListWidgetItem *current)
    {
        if (current == nullptr)
        {
            this->text_details->clear();
            return;
        }

        const int diagnostic_index = current->data(Qt::UserRole).toInt();
        showDiagnosticDetails(diagnostic_index);

        const std::optional<HydraulicSimulationResultTimeline> &result_timeline =
            this->hydraulic_data->simulationResultTimeline();
        if (!result_timeline.has_value() || diagnostic_index < 0
            || diagnostic_index >= result_timeline->diagnostics.size())
        {
            return;
        }

        const HydraulicSimulationDiagnostic &diagnostic =
            result_timeline->diagnostics.at(diagnostic_index);
        const InfrastructureEntity entity_type =
            diagnosticInfrastructureEntity(diagnostic, *this->hydraulic_data);
        if (entity_type != InfrastructureEntity::Unknown && !diagnostic.entity.uuid.isNull())
            this->hydraulic_data->setSelectedUuid(entity_type, diagnostic.entity.uuid);
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
    this->label_result_validity->clear();
    this->list_diagnostics->clear();
    this->text_details->clear();
    if (this->hydraulic_data == nullptr)
        return;

    const std::optional<HydraulicSimulationResultTimeline> &result_timeline = this->hydraulic_data->simulationResultTimeline();
    if (!result_timeline.has_value())
        return;

    const QList<HydraulicSimulationDiagnostic> &diagnostics = result_timeline->diagnostics;
    int warning_count = 0;
    int error_count = 0;
    for (const HydraulicSimulationDiagnostic &diagnostic : diagnostics)
    {
        if (diagnostic.severity == HydraulicSimulationDiagnosticSeverity::Warning)
            ++warning_count;
        else if (diagnostic.severity == HydraulicSimulationDiagnosticSeverity::Error
                 || diagnostic.severity == HydraulicSimulationDiagnosticSeverity::Fatal)
            ++error_count;
    }

    this->label_result_validity->setText(
        QStringLiteral("<b>Result validity:</b> <span style=\"color:%1; font-weight:600;\">%2</span>"
                       " &nbsp;·&nbsp; <b>Errors:</b> <span style=\"color:#d00000; font-weight:600;\">%3</span>"
                       " &nbsp;·&nbsp; <b>Warnings:</b> <span style=\"color:#c77800; font-weight:600;\">%4</span>")
            .arg(resultValidityColor(result_timeline->validity), resultValidityText(result_timeline->validity))
            .arg(error_count)
            .arg(warning_count));
    for (qsizetype index = 0; index < diagnostics.size(); ++index)
    {
        const HydraulicSimulationDiagnostic &diagnostic = diagnostics.at(index);
        const QString list_text = QStringLiteral("%1 · %2\n%3")
            .arg(severityText(diagnostic.severity), entityText(diagnostic), diagnostic.message);
        QListWidgetItem *item = new QListWidgetItem(list_text, this->list_diagnostics);
        item->setData(Qt::UserRole, static_cast<int>(index));

        const QColor color = severityColor(diagnostic.severity);
        if (color.isValid())
            item->setForeground(QBrush(color));
    }

    if (!diagnostics.isEmpty())
        this->list_diagnostics->setCurrentRow(0);
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

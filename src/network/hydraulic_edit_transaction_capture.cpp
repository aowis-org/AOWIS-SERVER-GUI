#include "network/hydraulic_edit_transaction_capture.h"

#include <aowis/model/serialization/model_codec.h>
#include <aowis/model/uuid.h>

#include <utility>

namespace
{
template<typename T>
bool findEncoded(const QList<T> &entities, const QUuid &uuid,
                 QString type, QString &foundType, std::optional<QByteArray> &foundBytes)
{
    for (const T &entity : entities)
    {
        if (entity.uuid != uuid)
            continue;
        foundType = std::move(type);
        foundBytes = aowis::model::ModelCodec<T>::encode(entity);
        return true;
    }
    return false;
}
}

HydraulicEditTransactionCapture::HydraulicEditTransactionCapture(QString operation, QString description)
    : transaction_id_(createUuidV7()), operation_(std::move(operation)), description_(std::move(description))
{}

HydraulicEditTransactionCapture::EntityImage HydraulicEditTransactionCapture::image(
    const NetworkHydraulic &network, const QUuid &uuid)
{
    EntityImage result;
    if (findEncoded(network.nodes_junctions, uuid, QStringLiteral("HydraulicNodeJunction"), result.type, result.data) ||
        findEncoded(network.nodes_reservoirs, uuid, QStringLiteral("HydraulicNodeReservoir"), result.type, result.data) ||
        findEncoded(network.nodes_tanks, uuid, QStringLiteral("HydraulicNodeTank"), result.type, result.data) ||
        findEncoded(network.demand_points, uuid, QStringLiteral("HydraulicDemandPoint"), result.type, result.data) ||
        findEncoded(network.links_pipes, uuid, QStringLiteral("HydraulicLinkPipe"), result.type, result.data) ||
        findEncoded(network.links_pumps, uuid, QStringLiteral("HydraulicLinkPump"), result.type, result.data) ||
        findEncoded(network.links_valves, uuid, QStringLiteral("HydraulicLinkValve"), result.type, result.data))
        return result;
    return result;
}

bool HydraulicEditTransactionCapture::watch(const NetworkHydraulic &network, const QUuid &uuid)
{
    if (uuid.isNull())
    {
        this->valid_ = false;
        return false;
    }
    if (!this->before_.contains(uuid))
        this->before_.insert(uuid, image(network, uuid));
    return true;
}

bool HydraulicEditTransactionCapture::watchCreated(const QUuid &uuid)
{
    if (uuid.isNull() || this->before_.contains(uuid))
    {
        this->valid_ = false;
        return false;
    }
    this->before_.insert(uuid, EntityImage{});
    return true;
}

std::optional<QByteArray> HydraulicEditTransactionCapture::finish(
    const NetworkHydraulic &network, QString *error) const
{
    if (error)
        error->clear();
    if (!this->valid_)
    {
        if (error)
            *error = QStringLiteral("Invalid or duplicate entity watch");
        return std::nullopt;
    }

    aowis::model::ModelEditTransaction transaction;
    transaction.transactionId = this->transaction_id_;
    transaction.operation = this->operation_;
    transaction.description = this->description_;
    for (QHash<QUuid, EntityImage>::const_iterator iterator = this->before_.cbegin();
         iterator != this->before_.cend(); ++iterator)
    {
        const EntityImage after = image(network, iterator.key());
        const EntityImage &before = iterator.value();
        if (before.type == after.type && before.data == after.data)
            continue;
        if ((before.data.has_value() && before.data->isEmpty()) ||
            (after.data.has_value() && after.data->isEmpty()) ||
            (before.data.has_value() && after.data.has_value() && before.type != after.type))
        {
            if (error)
                *error = QStringLiteral("Unable to capture consistent entity image");
            return std::nullopt;
        }
        aowis::model::ModelEntityEdit edit;
        edit.entityId = iterator.key();
        edit.entityType = before.data.has_value() ? before.type : after.type;
        edit.before = before.data;
        edit.after = after.data;
        transaction.edits.append(std::move(edit));
    }
    if (transaction.edits.isEmpty())
        return std::nullopt;
    const QByteArray encoded = aowis::model::ModelEditTransactionCodec::encode(transaction, error);
    if (encoded.isEmpty())
        return std::nullopt;
    return encoded;
}

#ifndef INFRASTRUCTURE_ENTITY_TRAITS_H
#define INFRASTRUCTURE_ENTITY_TRAITS_H

#include "common/_enums_structs.h"

namespace InfrastructureEntityTraits
{
constexpr bool isHydraulicConnectionNode(InfrastructureEntity entity) noexcept
{
    return entity == InfrastructureEntity::Junction ||
           entity == InfrastructureEntity::Reservoir ||
           entity == InfrastructureEntity::Tank;
}

constexpr bool isHydraulicDeviceLink(InfrastructureEntity entity) noexcept
{
    return entity == InfrastructureEntity::Pump || entity == InfrastructureEntity::Valve;
}

constexpr bool isHydraulicNetworkLink(InfrastructureEntity entity) noexcept
{
    return entity == InfrastructureEntity::Pipe || isHydraulicDeviceLink(entity);
}
}

#endif // INFRASTRUCTURE_ENTITY_TRAITS_H

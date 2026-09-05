#include "FaceCenteredVelocityGrid3.h"


Vector3 FaceCenteredVelocityGrid3::UPosition(
    std::size_t i,
    std::size_t j,
    std::size_t k) const
{
    return m_U.DataPosition(i, j, k);
}

Vector3 FaceCenteredVelocityGrid3::VPosition(
    std::size_t i,
    std::size_t j,
    std::size_t k) const
{
    return m_V.DataPosition(i, j, k);
}

Vector3 FaceCenteredVelocityGrid3::WPosition(
    std::size_t i,
    std::size_t j,
    std::size_t k) const
{
    return m_W.DataPosition(i, j, k);
}
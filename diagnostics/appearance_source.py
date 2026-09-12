"""Deterministic unit-cube source tables. Arrays use z,y,x (x contiguous).

The output is a density/temperature RATE, not a per-step increment. A future
coupled harness must multiply by physical dt and audit the actual GPU injection.
"""
import numpy as np


def sphere_rate(n, centre, quadrature=16, radius=.06, integral_rate=.002):
    if n not in (32,64,128) or quadrature not in (8,16,32):
        raise ValueError("Unsupported registered source resolution/quadrature")
    centre=np.asarray(centre,dtype=np.float64)
    if centre.shape!=(3,) or np.any(centre-radius<0) or np.any(centre+radius>1):
        raise ValueError("Source must lie entirely inside the unit cube")
    low=np.floor((centre-radius)*n).astype(int)
    high=np.minimum(n-1,np.floor((centre+radius)*n).astype(int))
    z,y,x=np.meshgrid(np.arange(low[2],high[2]+1),np.arange(low[1],high[1]+1),np.arange(low[0],high[0]+1),indexing="ij")
    cells=np.column_stack((x.ravel(),y.ravel(),z.ravel()))
    lo=cells/n-centre; hi=(cells+1)/n-centre
    far=np.maximum(np.abs(lo),np.abs(hi))
    near=np.maximum(np.maximum(lo,-hi),0)
    inside=(far*far).sum(axis=1)<=radius**2
    boundary=((near*near).sum(axis=1)<radius**2)&~inside
    fraction=inside.astype(np.float64)
    offsets=(np.arange(quadrature)+.5)/quadrature
    offsets=np.stack(np.meshgrid(offsets,offsets,offsets,indexing="ij"),axis=-1).reshape(-1,3)
    indices=np.flatnonzero(boundary)
    for start in range(0,len(indices),32):
        block=indices[start:start+32]
        d=(cells[block,None,:]+offsets[None,:,:])/n-centre
        fraction[block]=((d*d).sum(axis=-1)<=radius**2).mean(axis=1)
    raw_volume=float(fraction.sum()/n**3)
    if raw_volume<=0: raise ValueError("Empty source quadrature")
    rate=np.zeros((n,n,n),dtype="<f4")
    rate[cells[:,2],cells[:,1],cells[:,0]]=(fraction*integral_rate/raw_volume).astype("<f4")
    return rate,dict(centre=centre.tolist(),radius=radius,quadrature=quadrature,
                     raw_volume=raw_volume,analytic_volume=4*np.pi*radius**3/3,
                     integral=float(rate.sum(dtype=np.float64)/n**3),target_integral=integral_rate)

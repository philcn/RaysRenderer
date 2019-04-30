# RaysRenderer

Hybrid raytracing and filtering playground.

![Screenshot](./Screenshots/Screenshot.png?raw=true "Hybrid Raytracing")

## Features

* A selection of forward raster, deferred raster, hybrid (G-Buffer) raytracing and forward raytracing pipelines
* Raytraced reflection, shadow and AO
* Single component SVGF filter

## Future Work

### Rendering

* Shadowed analytical area light (Heitz 18)
* Emissive mesh light
* 1 bounce GGX diffuse global illumination
* Surfel global illumination (EA SEED 18)
* Raytraced irradiance fields (McGuire 19)

### Filters

* A-SVGF (Schied 18)
* Axis aligned shadow filter (Mehta 12)
* Anisotropic reflection filter (Liu 18)

## Dependencies

Falcor 3.2

## Setup Guide

Clone Falcor. Place RaysRenderer inside a folder (any name) that is sibling to Falcor. Open the solution and build.

```
Projects/
    Falcor/
    Apps/
        RaysRenderer/
```

## Reference

https://github.com/NVIDIAGameWorks/Falcor
https://cg.ivd.kit.edu/svgf.php

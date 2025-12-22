# AquaTerrainDX12 - Procedural Terrain Generation and Dynamic Water Simulation using DirectX 12

AquaTerrainDX12 is an evolving project focused on creating realistic procedural terrain and dynamic water simulation using DirectX 12. This project aims to explore ways to procedurally create dynamic, realistic and game-ready outdoor environments. 

Using my DirectX 12 framework built as the foundation of Nocte Engine, AquaTerrainDX12 uses a modern low-level graphcis API used as standard in advanced AAA games. 

Terrain is generated using Perlin noise algorithms and can be regenerated at runtime with parameters exposed to the user through an interactive GUI tool. The water is dynamic, with waves and ripples inspired by real-world physics, and has it's own shader and render pass to correctly render transparency and appropriate changes is visual properties based on it's depth.

Work on the project is ongoing. Screenshots of the project in it's current state can be found below.

![AquaTerrainDX12 Screenshot](./docs/images/AquaTerrain-NoGUI.png)

Figure 1: AquaTerrainDX12 - Procedural Terrain with Dynamic Water Simulation

![AquaTerrainDX12 Screenshot with GUI](./docs/images/AquaTerrain-GUIPreAdjustment.png)

Figure 2: AquaTerrainDX12 - Procedural Terrain with Dynamic Water Simulation and GUI with core parameters exposed for user adjustment

![AquaTerrainDX12 Screenshot with GUI Adjusted](./docs/images/AquaTerrain-GUIPostAdjustment.png)

Figure 3: AquaTerrainDX12 - Procedural Terrain with Dynamic Water Simulation and GUI with parameters adjusted by the user
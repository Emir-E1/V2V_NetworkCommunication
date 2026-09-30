# V2V Graph Coloring Simulator

A C++ tool simulating vehicle-to-vehicle (V2V) networks. It loads an OpenStreetMap area, moves 2,000+ vehicles along the road network, and builds a dynamic interference graph. Graph coloring algorithms (greedy, Welsh–Powell, DSATUR, local search) assign radio channels to vehicles to avoid conflicts, and are compared on channel count, computation time, and allocation stability.

> Master 1 – Mobile Networks project, Université de Haute-Alsace (Autumn 2026).
> Supervisor: Laurent Moalic.

## Features

- **Interactive map**: load an OpenStreetMap area, zoom, pan, select a simulation zone, display the road network.
- **Road graph**: intersections, road segments, directions, distances.
- **Vehicle simulation**: 2,000+ vehicles, each with position, speed, direction, destination and transmission radius. Time controls: start, pause, speed up, slow down, reset.
- **Dynamic interference graph**: two vehicles are linked when their distance is below the transmission range; the graph is displayed on the map and recomputed during the simulation.
- **Graph coloring**: greedy, Welsh–Powell, DSATUR, local search. Each coloring is checked automatically (no two neighbors share a color).
- **Comparison**: number of colors, execution time, evolution over time, stability of the channel allocation (number of vehicles changing color between two recomputations).

## Prerequisites

- C++17 compiler (GCC, Clang or MSVC)
- CMake 3.16+
- Qt 6 (Widgets)
- Git
- An OpenStreetMap export (`.osm`) of the area to simulate (from openstreetmap.org or the Overpass API)

## Build

```bash
git clone [repository URL]
cd [project folder]
mkdir build && cd build
cmake ..
cmake --build .
```

## Run

```bash
./v2v-simulator [path/to/map.osm]
```

Without an argument, use **File > Open map** in the application to load an `.osm` file. Sample data is provided in the `data/` folder.

## Usage

1. Load a map and select the simulation zone.
2. Set the number of vehicles and the transmission radius in the right-hand panel.
3. Press **Start** to run the simulation (use Pause, Slow down, Speed up and Reset as needed).
4. Choose a coloring algorithm and press **Color**. Vehicles are displayed with their assigned color.
5. Press **Compare** to run all algorithms on the current graph and view the results.
6. Results can be exported to CSV.

## Project structure

```
.
├── src/
│   ├── map/            # OSM loading, map rendering
│   ├── roadgraph/      # road network graph
│   ├── simulation/     # vehicles, movement, time control
│   ├── interference/   # dynamic interference graph
│   ├── coloring/       # greedy, Welsh–Powell, DSATUR, local search
│   └── ui/             # Qt interface
├── data/               # sample OSM files
├── docs/               # specification, slides, user/developer manual
├── tests/              # tests of the coloring algorithms
├── CMakeLists.txt
└── README.md
```



## Team

| Name | Role |
|------|------|
| [Member 1] | [Role] |
| [Member 2] | [Role] |
| [Member 3] | [Role] |
| [Member 4] | [Role] |

## Use of generative AI

The use of generative AI tools in this project is documented in the user/developer manual (`docs/`). All code integrated in the project has been reviewed and is understood by the team.

## References

- D. J. A. Welsh, M. B. Powell, *An upper bound for the chromatic number of a graph and its application to timetabling problems*, The Computer Journal, 1967.
- D. Brélaz, *New methods to color the vertices of a graph*, Communications of the ACM, 1979.
- OpenStreetMap: https://www.openstreetmap.org
- Qt documentation: https://doc.qt.io

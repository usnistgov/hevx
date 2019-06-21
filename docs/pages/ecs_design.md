ECS Design
==========

## Entity Component System (ECS) Overview
TODO: list some references here

### Entity
- NO data
- NO methods
- JUST a name
- Really just a globally unique number (GUID)

### Components
- ONLY data

### Systems
- ONLY methods

## Components
- Renderable
- Inputable

### Renderable
- transformation matrix
- model-specific uniform buffer (matrices; shader variables)
- pipeline
- descriptor set
- number of vertices to draw
- vertex buffer
- number of indices to draw
- index buffer

## Systems
- Rendering
  - Renders all entities that have the Renderable component
- Input
  - Updates Inputable entities
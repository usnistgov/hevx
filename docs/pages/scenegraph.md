Scenegraph
==========

```
                                 +-------+
                                 | scene |
                                 +-------+
                                     |
    +------------+------------+------------+------------+------------+
    |            |            |            |            |            |
+-------+    +-------+    +-------+    +-------+    +-------+    +-------+
| ether |    |  nav  |    | pivot |    |  head |    |  wand |    | light |
+-------+    +-------+    +-------+    +-------+    +-------+    +-------+
                 |
             +-------+
             | world |
             +-------+
```

* **scene**
  - root of scenegraph
  - normalized units
  - nodes under **scene** do not move with navigation

* **ether**
  - contains same transformation as **world**
  - model units
  - nodes under **ether** do not move with navigation

* **nav**
  - normalized units
  - nodes under **nav** move with navigation
  - **world**
    - model units
    - typical use is to set transform of **world** to accomodate models and load
      all models underneath **world**

* **pivot**
  - normalized units, reflect the position and orientation of the pivot point
  - nodes under **pivot** move with navigation

* **head**
  - position of head in virtual world
  - normalized units
  - nodes under **head** do not move with navigation, do move with head motions

* **wand**
  - position of wand in virtual world
  - normalized units
  - nodes under **wand** do not move with navigation, do move with wand motions

* **light**
  - default light: above and behind the viewer

## Purpose of **nav** node

Typical 3D applications move a _camera_ around the world based on user input.
Instead of moving a camera, HEVx uses a stationary camera and instead moves
all the objects the camera is looking at. The **nav** node in the scenegraph
is updated based on navigation (i.e. user input) thereby moving all nodes
underneath it.


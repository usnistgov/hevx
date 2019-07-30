IRIS Control Commands
=====================

## Overview

Control commands can be in an iris file, read by the iris file loader, or can
be sent to a running iris application via `$IRIS_CONTROL_FIFO`.

Keywords are shown in **UPPERCASE** and parameters are shown in _italics_.
Specific parameter values are shown in `Courier`.

When _nodeName_ is a parameter, the special node name **-** can be used to
refer to the last node loaded by the **LOAD** command. Also see the **LOAD**
_nodeName_ _fileName_ command for an additional use of the **-** node name.

## Commands

**BACKGROUND** _r g b_  
sets the background color for every pane
- _r g b_ are each between 0 and 1 inclusive

**ECHO** _ON_ | _OFF_  
enables or disable the echoing of incoming commands

**TERMINATE**  
terminates iris

**NAV** `POSITION` _x y z_  
set the navigation position to _x y z_

**NAV** `ORIENTATION` _h p r_  
set the navigation orientation to the Euler angle _h p r_

**NAV** `ATTITUDE` _x y z w_  
set the navigation orientation to the quaternion _x y z w_

**NAV** `SCALE` _x_ [_y z_]  
set the navigation scale to _x y z_
- if _y_ and _z_ are omitted a uniform scale of _x_ is applied

**NAV** `MATRIX` _a00 a01 a02 a03 a10 a11 a12 a13 a20 a21 a22 a23 a30 a31 a32 a33_  
set the entire navigation matrix

**NAV** `RESPONSE` _s_  
set the navigation response to _s_

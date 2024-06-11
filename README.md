# New VDE Switch architecture

## Modular Architecture

The new VDE switch employs a modular architecture, which provides flexibility and extensibility. The switch is designed to load and unload modules dynamically at runtime, allowing for easy addition or removal of functionalities without affecting the core operation of the switch.

Each module in the switch is self-contained, encapsulating a specific functionality. This design allows for a clear separation of concerns, making the codebase easier to maintain and extend. Modules can be identified and managed individually, thanks to a unique `module_tag` associated with each module.

The architecture separates the management plane and the data plane, each residing in its own directory. The management plane handles configuration and control tasks, while the data plane is responsible for the actual data forwarding.

The switch operates based on a main event loop, which is defined by some module (i.e., the data plane module of choice).

This modular architecture not only makes the switch highly customizable and adaptable to various network environments, but also facilitates future development and maintenance.

## Module Interface

Each module in the switch is required to implement a set of functions, which define the module interface. The module interface includes functions for module initialization, configuration, and cleanup, as well as functions for handling input output.

Additionally, each module can "ask" the switch to gain access to other modules, so that it can interact with them. As an example, the cli (console) module asks all the other module for a list of commands that they can handle, so that it can provide a unified command line interface to the user.

## Configuration / Options

The switch initial configuration can happen both via command line arguments and/or a .json configuration file. Each module can define its own configuration options, which are then parsed by the switch and passed to the corresponding module.

## Data Plane

The data plane is responsible for the actual data forwarding. It is implemented as a module, which can be replaced with a different data plane module at runtime. The data plane module is responsible for handling incoming packets, making forwarding decisions, and sending packets out.


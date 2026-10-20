# Technical guide

This companion to the [tutorial](../README.md) covers configuration, the dynamics calculation, runtime behavior, and the optional C++ and Cartesian examples. Run commands from the `tutorials/rebot-gravity-compensation` directory.

## Configuration

Fetch the robot model and install the Python dependencies using the [README setup](../README.md#set-up-the-python-example). Python uses the official motorbridge wheel and its bundled driver; it needs no C++ or Rust build. Leave `MOTORBRIDGE_LIB` unset for hardware runs. Tests use that override to select a fake driver.

[config/rebotarm_dm.yaml](../config/rebotarm_dm.yaml) defines the serial device, motor IDs and models, URDF path, arm/gripper groups, watchdog, and gravity gains. Check the device and IDs against your arm before running. The account needs access to the serial device; `/dev/serial/by-id/...` can provide a stable path across reconnects.

Motor names map to URDF joint names, rather than assuming the same ordering. Relative URDF paths are resolved beside the YAML, then from its parent directory. `tau_scale` and `joint_direction` each contain six values in arm-group order; directions must be +1 or −1. Use decimal motor IDs and quote model names such as `"4340P"`. Duplicate YAML keys, unknown gravity parameters, and invalid gains are rejected.

Python validates the YAML with `hsdk.config.parse()`, reads it through PyYAML, and supplies operator settings through `hsdk.Param`. C++ uses `holoscan::config_io::load()`, `params::decode<GravityParams>()`, and `set_params()`. Parameters remain fixed during a run.

## C++ build

Install HSDK 5.0 with its development components, a C++20 compiler, CMake 4 or newer, Ninja, Git, Rust/Cargo, and Python 3.12 or newer. The tested SDK build is 5.0.0.7. Setup needs network access to clone the pinned Seeed model repository, install Python wheels, clone motorbridge, and download its locked Cargo dependencies from crates.io. Then run:

```bash
python3.12 scripts/fetch-urdf.py
./scripts/bootstrap-deps.sh
cmake --preset default
cmake --build --preset default
ctest --preset default
```

The bootstrap uses `python3.12` by default and installs Pinocchio's native libraries and development files from wheels into `.venv`. If your Python 3.12-or-newer interpreter has another name, select it with `REBOT_PYTHON=/path/to/python ./scripts/bootstrap-deps.sh`; the system `python3` may be older. The pinned dependencies in [requirements-build.txt](../requirements-build.txt) keep compatible library versions together. Keep this environment in place while using the build.

The build preset uses two parallel jobs to limit peak memory use. On a host with more memory, override it with `cmake --build --preset default --parallel N`.

CMake uses the installed `holoscan` package, its exported targets, and `holoscan_add_flatbuffer_schema()`. For a nonstandard SDK installation, pass `-DCMAKE_PREFIX_PATH=/path/to/holoscan/install` when configuring. The C++ build fetches and builds unmodified motorbridge with Cargo; the Python example obtains the same driver version from pip. Both retain the stock driver's behavior.

With the arm at home, run `./build/rebot_gravity_compensation`, optionally followed by a configuration path. Return the arm home manually before Ctrl+C. The executable runs from its build tree; there is no standalone installation bundle.

## Graph and resource lifetime

The [Python application](../python/gravity_compensation.py) defines the three operators and wires their ports. The [C++ application](../apps/gravity_compensation.cpp) uses the equivalent operators in [src/operators](../src/operators). `OnClock` requests a source tick every 2.5 ms; `OnEach` triggers the two downstream operators. A single `control` partition serializes their callbacks. Depth-one, drop-oldest queues and coalesced late ticks avoid a backlog, but do not guarantee a 400 Hz hardware update rate.

Python passes immutable dataclasses through `hsdk.pyobject` ports. C++ uses generated FlatBuffers messages. The Python port type does not validate the dataclass schema or provide cross-process serialization. Neither implementation modifies an emitted message.

Python closures retain the shared robot and controller. C++ passes `std::shared_ptr` objects as operator constructor arguments. These resources are not operator parameters; the name supplied to `graph.op()` identifies a graph node, not a device registry entry.

Compilation validates the graph before any device is opened. `prepare_gravity_compensation()` then loads the model, reads the initial pose, configures the motors, and sends the initial gravity command. Start `run_async()` immediately afterward because the firmware watchdog is already armed.

## Gravity model and control law

For a fixed-base robot, omitting friction and external forces, inverse dynamics computes joint torque from position `q`, velocity `q̇`, and acceleration `q̈`:

```text
τ = M(q) q̈ + c(q, q̇) + g(q)
```

`M` is the inertia matrix, `c` contains velocity-dependent forces, and `g` is the gravity load. Pinocchio's Recursive Newton–Euler Algorithm (RNEA) calculates motion outward through the links, then accumulates forces and moments back toward the base. Gravity compensation uses the special case `g(q) = RNEA(q, 0, 0)` through `computeGeneralizedGravity()`. It supports the modeled weight while leaving inertia and friction present.

[rebot_gravity.py](../python/rebot_gravity.py) calls `buildModelFromUrdf()`, creates a reusable `Data` workspace, and initializes `q` with `pin.neutral()`. Joint-name lookup supplies separate configuration and velocity/torque indices. Each tick updates the six arm coordinates and extracts their gravity torques. The gripper body's mass remains in the model; the two massless finger joints stay at model-neutral positions. This reference configuration does not command a physical motion.

For each arm joint, the configured direction `s` and torque scale `α` give:

```text
q_model = s × q_motor
τ_feedforward = s × α × g(q_model)
```

The direction must also apply to torque to preserve joint work. Gravity is `(0, 0, −9.81)` m/s² in the fixed base frame. A different mounting orientation or added payload requires the corresponding model change.

URDF joint origins and axes express the geometry that might otherwise be described by DH parameters. Masses and center-of-mass placements determine gravity torque; rotational inertia tensors matter during acceleration. A static gravity test therefore checks geometry, calibration, and mass distribution, but cannot validate every inertia parameter. The [fetch script](../scripts/fetch-urdf.py) obtains the model from Seeed and removes visual and collision geometry and rendering materials. The generated model is ignored by Git; [model provenance](../urdf/SOURCES.md) records its origin and checksum.

The motors implement the MIT control law:

```text
τ_motor = kp × (q_reference − q) + kd × (velocity_reference − velocity) + τ_feedforward
```

The gravity example sets the reference to the latest observed position, requests zero velocity, and uses `kp=1.5`, `kd=1.0` by default. These gains and gravity feedforward apply from the first command, with no startup ramp. The gripper retains its configured gains and receives zero feedforward torque. Gravity feedforward is checked against motor and URDF effort limits; the motor's additional position and damping terms still contribute to total effort.

## Startup, watchdogs, and feedback

Every motor must report disabled before setup. Preparation programs and reads back the CAN watchdog, checks that the initial command matches the observed pose, and enables the motors. Faulted or already-enabled motors cause startup to fail; the application does not automatically clear faults.

Commands carry the source sequence and host timestamp. The controller and sink reject nonfinite values, invalid limits, out-of-order samples, and excessive processing age. The driver validates a complete batch before sending any of it. The main thread checks command completion about every 5 ms; a gap beyond `watchdog_ms` (100 ms by default) stops the graph and attempts to disable the motors. Firmware watchdogs provide a command-loss fallback if the host stalls or communication fails.

Stock motorbridge returns cached feedback without a packet counter or receive timestamp. Our timestamp dates the cache read, not the physical measurement. If incoming feedback stops while commands still succeed, the loop can continue using old values; neither command watchdog detects that condition. The example uses unmodified motorbridge and retains this limitation.

SIGINT/SIGTERM handlers record a stop request; the supervising thread requests cooperative graph shutdown. The sink and final cleanup disable only motors this process enabled, including partial startup. Stopping removes active weight support. Start at the arm's power-off-friendly home position and return it there manually before normal shutdown; the application never homes the arm automatically.

## Cartesian point hold

[python/cartesian_hold.py](../python/cartesian_hold.py) extends gravity compensation with reusable kinematics and impedance operators:

```text
JointStateSourceOp → GravityTorqueOp → PinocchioKinematicsOp → CartesianImpedanceOp → MotorCommandSinkOp
```

Run it with the same Python environment:

```bash
build/python-venv/bin/python python/cartesian_hold.py
```

The arm starts in gravity compensation. Guide it to a pose, then enter `h` followed by Enter to hold the current tool position. Enter `g` followed by Enter to release it. Release the hold and return home manually before Ctrl+C. The held point is the origin of the URDF's `end_link` frame in base coordinates; orientation remains free.

Each stage carries the original observation forward in an immutable `ControlSample`. Kinematics owns a separate Pinocchio `Data` workspace and uses `computeJointJacobians()`, `updateFramePlacements()`, and `getFrameJacobian()` to obtain the translational Jacobian `J`. The controller applies:

```text
tool_velocity = J @ joint_velocity
force = stiffness × (target − tool_position) − damping × tool_velocity
task_torque = J.T @ force
```

The sink adds task torque to gravity, applies joint damping and torque budgets, then builds and sends motor commands. It scales the task contribution when necessary to preserve gravity within the budget. [config/cartesian_hold.yaml](../config/cartesian_hold.yaml) sets stiffness, damping, force, position-error, speed, and torque limits. These bound calculated commands, not measured contact force. All five operators share one control partition; the graph wiring is in the application and the reusable factories are in [cartesian_operators.py](../python/cartesian_operators.py).

## Profiling and CPU placement

The README's Nsight commands work for both Python and C++; substitute `./build/rebot_gravity_compensation` to profile C++. The runtime supplies NVTX ranges and message markers. `nvtx_sum` reports elapsed callback durations, including waits. Stock motorbridge spaces transmissions by at least 120 µs for multiple motors, so sending seven commands includes six pacing gaps. This accounts for much of the motor callback's elapsed time.

To inspect message correlation in a SQLite export:

```bash
nsys export --type sqlite --include-blobs=true \
  -o build/profiles/gravity-python.sqlite build/profiles/gravity-python.nsys-rep
```

Match each edge's source identity and publication sequence, then follow the gravity callback's receive-to-emit relationship. Counting rows or pairing events by time alone can associate different observations. The README measures source entry to matching sink entry; including sink completion adds command transmission. Source-period jitter measures the variation between consecutive source entries around 2.5 ms. Neither measures physical sensor-to-actuator latency, and callback-duration variation is not the same as update jitter.

Both gravity applications accept `--cpu`, using `CompileOptions.deployment.bind_thread_placement()` with a fail-admission policy. Choose an available logical CPU using `lscpu -e=CPU,CORE,SOCKET,NODE`. For example, if CPU 2 is available:

```bash
build/python-venv/bin/python python/gravity_compensation.py --cpu 2
./build/rebot_gravity_compensation --cpu 2
```

Affinity fixes placement; reserving the CPU from other tasks is a separate Linux operation. An administrator can create a cgroup v2 partition with `cpuset.cpus.partition=isolated`, reserving entire physical cores and their SMT siblings. Put helper threads on another reserved core and keep the control partition on its own. This can be changed at runtime without rebuilding the kernel or changing boot parameters. IRQs, timers, and per-CPU kernel work can still interrupt execution. See the [Linux cpuset documentation](https://www.kernel.org/doc/html/latest/admin-guide/cgroup-v2.html#cpuset).

## Tests without hardware

After fetching the model and completing the C++ build above, run the Python tests using the same fake motor ABI:

```bash
build/python-venv/bin/python tests/test_python_gravity.py
build/python-venv/bin/python tests/test_python_cartesian.py
```

The suites cover reference gravity torques, Jacobian finite differences, control limits, configuration, graph execution, and cleanup after faults or signals. They explicitly select the fake driver and do not open the arm. Fault-injection cases intentionally print runtime error diagnostics.

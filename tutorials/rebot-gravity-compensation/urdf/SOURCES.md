# Robot model provenance

The Seeed model is not distributed in this repository because its license provenance is unconfirmed. No license file was present at the pinned upstream revision. Fetching it does not establish redistribution rights; review upstream terms before using it in a distributed artifact.

From `tutorials/rebot-gravity-compensation`, run:

```bash
python3.12 scripts/fetch-urdf.py
```

The [script](../scripts/fetch-urdf.py) requires Git and network access. It clones [Seeed Studio's repository](https://github.com/Seeed-Projects/reBotArm_control_py) into a temporary directory and reads `urdf/DM/urdf/ReBot_Arm_DM.urdf` at commit `1bcd81b22c182ec257bf04f5746e1e3d556a5f1c`. It verifies the source SHA-256 before parsing or writing the model:

`cd3054edec30b70a3afb2e97eb288ad4943a31c9746f8775703733b64ac0c3cf`

The generated `urdf/DM/urdf/ReBot_Arm_DM.urdf` preserves all joint definitions, link inertias, limits, and frames. Visual/collision geometry and rendering materials are omitted because the dynamics calculation does not use meshes. The two massless finger joints remain at model-neutral positions when the six arm joints are selected. This model provides no collision geometry.

The temporary clone is removed when the script exits. The generated `urdf/DM/` directory is ignored by Git and must not be committed or included in distributed packages or images. Rerunning the script replaces the generated model, so keep any local model customizations elsewhere and set `urdf_path` in your configuration accordingly.

# models_nam

Drop-in folder for new NAM amp models, as delivered from TONE3000 or exported from a NAM trainer: `.nam` files, or `.zip` files with a `.nam` file in them.

- Only A2 Nano models can be used.
- Run `./pedal_setup` and open **NAM setup** (from `Software/GuitarPedal`). It lists the files in this folder that have not been installed yet. Select the ones you want and it asks for a display name (at most 14 characters, suggested from the name and gear model in the `.nam` file), converts them with `nam_to_cpp_array.py` into one header each in [../models/](../models/), and updates the list of built-in models.
- The converted headers in `../models/` are what the firmware is built from. The files in this folder are only the source material and are not used by the build.
- Which models are built in is chosen in NAM setup and saved in `../models.config`. Every built-in model takes flash and memory, see [docs/PERFORMANCE_AND_MEMORY.md](../../../docs/PERFORMANCE_AND_MEMORY.md).

The `.zip` files are not committed (see `.gitignore` in this folder), since they are the downloaded originals. They can be large, and their licenses are very likely incompatible with this repository's, so do not commit downloaded models.

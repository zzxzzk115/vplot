/* Gallery metadata for the web-capable examples. Hand-written on purpose:
   descriptions need human wording, and keeping it a plain script (not fetched
   JSON) lets the gallery work from file:// during local preview.
   When a new web-capable example lands, append one entry here. */

var VPLOT_EXAMPLES = [
    { name: "demo",      title: "Demo Browser",   desc: "Every supported feature, one panel each — pick a demo, tweak it live, and the figure re-rasterizes into an ImGui panel. Modeled on ImGui's own demo window.", tags: ["imgui", "gallery"] },
    { name: "vri_imgui", title: "ImGui Embedding", desc: "The minimal embedding: a single vplot figure rendered to an RGBA buffer, uploaded as a texture, and drawn inside a Dear ImGui window.",                       tags: ["imgui", "minimal"] }
];

// components/tags.h
#pragma once

#include <string_view>

namespace referentia::components {

/**
 * Tags.
 *
 * Zero-sized marker components. An entity's category is a component, not a C++
 * type or a field on some other component, so a query can select by category
 * (`ecs.view<components::TagNode>()`) and run the right system without every
 * system having to know which struct happens to be present.
 *
 * Tag <component> combinations are meaningful: an entity is a camera if it has
 * TagCamera, and it is the board root if it has TagWorldRoot. Entities are
 * otherwise classified structurally (e.g. a node is a node by having
 * components::NodeTransform), so the tags carry intent, not identity.
 */
struct TagCamera {
  static constexpr std::string_view Name = "TagCamera";
};

/**
 * The root of the board's entity tree. Every node and group hangs off exactly
 * one of these, so ownership queries ("everything on the board") have an
 * anchor to walk from instead of matching on component sets.
 */
struct TagWorldRoot {
  static constexpr std::string_view Name = "TagWorldRoot";
};

/**
 * A reference node: one placed image, text or shape. Nodes are the atomic
 * things a group can contain.
 */
struct TagNode {
  static constexpr std::string_view Name = "TagNode";
};

/**
 * A group. Groups are flat tag-based collections, not parent/child
 * hierarchies, but they are still entities so their frame, tint and label
 * have somewhere to live and can be addressed without a side table.
 */
struct TagGroup {
  static constexpr std::string_view Name = "TagGroup";
};

/**
 * Screen-space diagnostic overlay, e.g. the temporary pan/zoom verification
 * HUD. Overlays are entities rather than direct draw calls from the app loop so
 * they can be toggled, inspected and eventually disabled per board.
 */
struct TagOverlay {
  static constexpr std::string_view Name = "TagOverlay";
};

}  // namespace referentia::components

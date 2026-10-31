import math
import unittest

from ricochet_lab.visibility import MAX_ENTITIES, MAX_OCCLUDERS, visible_entities


def entity(identifier, position):
    return {"id": identifier, "position": position}


class VisibilityTests(unittest.TestCase):
    def test_range_boundary_order_and_copies(self):
        entities = [entity("edge", [3, 4, 0]), entity("self", [0, 0, 0]),
                    entity("far", [5.001, 0, 0])]
        result = visible_entities("self", entities, max_distance=5)
        self.assertEqual([e["id"] for e in result], ["edge", "self"])
        result[0]["position"][0] = 999
        self.assertEqual(entities[0]["position"][0], 3)

    def test_occluded_parallel_outside_and_box_beyond_target(self):
        entities = [entity("self", [0, 0, 0]), entity("blocked", [4, 0, 0]),
                    entity("side", [4, 4, 0]), entity("near", [0.5, 0, 0])]
        box = {"min": [1, -0.25, -1], "max": [2, 0.25, 1]}
        result = visible_entities("self", entities, max_distance=10, occluders=[box])
        self.assertEqual([e["id"] for e in result], ["self", "side", "near"])

    def test_closed_box_tangent_and_own_entity_inside(self):
        entities = [entity("self", [0, 0, 0]), entity("other", [1, 0, 0])]
        box = {"min": [0, 0, 0], "max": [1, 1, 1]}
        result = visible_entities("self", entities, max_distance=10, occluders=[box])
        self.assertEqual(result, [entity("self", [0.0, 0.0, 0.0])])

    def test_reversed_segment_and_zero_range(self):
        entities = [entity("self", [4, 0, 0]), entity("other", [0, 0, 0]),
                    entity("same", [4, 0, 0])]
        box = {"min": [1, -1, -1], "max": [2, 1, 1]}
        self.assertEqual(
            [e["id"] for e in visible_entities("self", entities,
                                              max_distance=10, occluders=[box])],
            ["self", "same"],
        )
        self.assertEqual(
            [e["id"] for e in visible_entities("self", entities, max_distance=0)],
            ["self", "same"],
        )

    def test_invalid_entities_and_configuration(self):
        base = [entity("self", [0, 0, 0])]
        for distance in (-1, True, math.inf, math.nan, 1e10, "10"):
            with self.subTest(distance=distance), self.assertRaises(ValueError):
                visible_entities("self", base, max_distance=distance)
        for entities in (
            [], base + base, [entity("self", [0, math.nan, 0])],
            [entity("self", [0, False, 0])], [entity("self", [0, 0])],
            [{"id": "self", "position": [0, 0, 0], "always_visible": True}],
            [entity("self", [0, 10**1000, 0])], [entity("bad id", [0, 0, 0])],
            None,
        ):
            with self.subTest(entities=entities), self.assertRaises(ValueError):
                visible_entities("self", entities, max_distance=10)

    def test_invalid_boxes_and_resource_bounds(self):
        base = [entity("self", [0, 0, 0])]
        for boxes in (
            [{"min": [1, 0, 0], "max": [0, 0, 0]}],
            [{"min": [0, 0, 0]}], None,
            [{"min": [0, 0, 0], "max": [1, 1, 1]}] * (MAX_OCCLUDERS + 1),
        ):
            with self.subTest(boxes=boxes), self.assertRaises(ValueError):
                visible_entities("self", base, max_distance=1, occluders=boxes)
        oversized = (entity(str(i), [0, 0, 0]) for i in range(MAX_ENTITIES + 1))
        with self.assertRaises(ValueError):
            visible_entities("0", oversized, max_distance=1)


if __name__ == "__main__":
    unittest.main()

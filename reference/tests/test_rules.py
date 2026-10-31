import math
import unittest

from ricochet_lab.rules import RuleEngine, analyze_events


def movement(tick, time, x):
    return {"kind": "movement", "payload": {"tick": tick, "time": time, "position": [x, 0, 0]}}


def shot(tick, time):
    return {"kind": "shot", "payload": {"tick": tick, "time": time}}


class RuleTests(unittest.TestCase):
    def test_movement_at_boundary_and_rejection_keeps_baseline(self):
        engine = RuleEngine(max_speed=8)
        self.assertEqual(engine.analyze([movement(0, 0, 0), movement(1, 1, 8)]), [])
        findings = engine.analyze([movement(2, 2, 30)])
        self.assertEqual(findings[0]["rule_id"], "LAB_MOVEMENT_SPEED")
        self.assertEqual(engine.analyze([movement(3, 3, 24)]), [])

    def test_movement_order_is_strict(self):
        engine = RuleEngine()
        engine.analyze([movement(5, 5, 0)])
        for event in (movement(5, 6, 0), movement(6, 5, 0), movement(4, 4, 0)):
            self.assertEqual(engine.analyze([event])[0]["rule_id"], "LAB_MOVEMENT_ORDER")
        self.assertEqual(engine.analyze([movement(6, 6, 0)]), [])

    def test_shot_state_persists_and_rejected_shots_do_not_consume_ammo(self):
        engine = RuleEngine(initial_ammo=2, min_shot_interval=.1)
        self.assertEqual(engine.analyze([shot(0, 0)]), [])
        self.assertEqual(engine.analyze([shot(1, .05)])[0]["rule_id"], "LAB_SHOT_RATE")
        self.assertEqual(engine.remaining_ammo, 1)
        self.assertEqual(engine.analyze([shot(1, .1)]), [])
        self.assertEqual(engine.remaining_ammo, 0)
        self.assertEqual(engine.analyze([shot(2, .2)])[0]["rule_id"], "LAB_AMMO_EMPTY")

    def test_shot_order_and_floating_boundary(self):
        engine = RuleEngine(min_shot_interval=.1)
        self.assertEqual(engine.analyze([shot(1, .1), shot(2, .2), shot(3, .3)]), [])
        for event in (shot(3, .4), shot(4, .3), shot(2, .2)):
            self.assertEqual(engine.analyze([event])[0]["rule_id"], "LAB_SHOT_ORDER")

    def test_entitlements_are_operator_owned(self):
        entitlements = {"demo.rifle"}
        engine = RuleEngine(entitlements=entitlements)
        entitlements.add("demo.premium")
        self.assertEqual(engine.analyze([{"kind": "equip", "payload": {"item": "demo.rifle"}}]), [])
        result = engine.analyze([{"kind": "equip", "payload": {"item": "demo.premium"}}])
        self.assertEqual(result[0]["rule_id"], "LAB_ENTITLEMENT_MISSING")
        injected = {"kind": "equip", "payload": {"item": "demo.premium", "entitlements": ["demo.premium"]}}
        self.assertEqual(engine.analyze([injected])[0]["rule_id"], "LAB_EVENT_INVALID")

    def test_client_cannot_supply_policy_or_ammo_reset(self):
        engine = RuleEngine(initial_ammo=0)
        injected = shot(1, 1)
        injected["payload"]["ammo"] = 100
        self.assertEqual(engine.analyze([injected])[0]["rule_id"], "LAB_EVENT_INVALID")
        self.assertEqual(engine.remaining_ammo, 0)
        injected = movement(1, 1, 0)
        injected["payload"]["max_speed"] = 1e9
        self.assertEqual(engine.analyze([injected])[0]["rule_id"], "LAB_EVENT_INVALID")

    def test_nonfinite_booleans_huge_and_malformed_values(self):
        events = [None, {}, {"kind": "movement", "payload": []}, shot(True, 1), shot(1, True)]
        for bad in (math.nan, math.inf, -math.inf, -1, 10**1000):
            events.append(shot(1, bad))
        for bad in (True, math.nan, math.inf, 10**1000, "zero"):
            events.append(movement(1, 1, bad))
        events += [{"kind": "movement", "payload": {"tick": 1, "time": 1, "position": [0, 0]}},
                   {"kind": "equip", "payload": {"item": ""}}]
        for event in events:
            with self.subTest(event=event):
                self.assertEqual(analyze_events([event])[0]["rule_id"], "LAB_EVENT_INVALID")

    def test_kernel_observations_have_no_gameplay_verdict(self):
        self.assertEqual(analyze_events([{"kind": "kernel_observation", "payload": {"pid": 1234}}]), [])
        unknown = analyze_events([{"kind": "future_observation", "payload": {}}])[0]
        self.assertEqual(unknown["severity"], "info")
        self.assertEqual(unknown["rule_id"], "LAB_EVENT_UNSUPPORTED")

    def test_bad_server_configuration(self):
        for config in ({"max_speed": 0}, {"max_speed": math.nan}, {"max_speed": 10**1000},
                       {"initial_ammo": True}, {"initial_ammo": -1}, {"min_shot_interval": -1},
                       {"entitlements": "demo.rifle"}, {"entitlements": [""]}):
            with self.subTest(config=config), self.assertRaises(ValueError):
                RuleEngine(**config)

    def test_batch_bounds_and_determinism(self):
        with self.assertRaises(ValueError):
            analyze_events([shot(0, 0)] * 257)
        events = [movement(0, 0, 0), movement(1, 1, 100), {"kind": "equip", "payload": {"item": "locked"}}]
        self.assertEqual(analyze_events(events), analyze_events(events))


if __name__ == "__main__":
    unittest.main()

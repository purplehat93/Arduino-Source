import copy
import unittest

import collection
import execution_policy
import observations
import quality
import reference_catalog
from test_execution_policy import supported_workspace


def set_judge(value, item, labels):
    stats = {}
    for stat, label in zip(observations.STATS, labels):
        lo, hi, trained = observations.RATINGS[label]
        stats[stat] = dict(status='read', rating=label, raw=label, raw_confirmation=label,
                           iv_min=lo, iv_max=hi, hyper_trained=trained)
    judge = dict(status='read', stats=stats)
    observed = value['source_inventory']['pokemon'][item]
    observed['collection_observation'].update(judge=judge, ivs=observations.validate_judge(judge))


def duplicate_copies():
    value = supported_workspace()
    first, second = 'pokemon-00000', 'pokemon-00001'
    value['source_inventory']['pokemon'][second] = copy.deepcopy(value['source_inventory']['pokemon'][first])
    value['annotations'][second] = copy.deepcopy(value['annotations'][first])
    set_judge(value, first, ['Hyper trained!'] * 6)
    set_judge(value, second, ['Best'] * 6)
    return value


def profile(policy=None):
    entry = dict(id='iv-pilot', kind='custom', form_keys=['pichu'], start_box=1)
    if policy is not None: entry['quality'] = policy
    return dict(version=1, collections=[entry])


class QualityTests(unittest.TestCase):
    def test_manual_selection_stays_manual_and_source_is_preserved(self):
        value = duplicate_copies(); before = copy.deepcopy(value)
        plan = collection.move_plan(value, reference_catalog.build_reference(), profile(),
                                    enable_execution=True, verify_judge=True)
        self.assertEqual(plan['collection_review']['assignments'][0]['occurrence'], 'pokemon-00000')
        self.assertEqual(plan['collection_execution']['mode'], 'metadata_form_judge_v1')
        self.assertEqual(value, before)
        self.assertFalse(plan['collection_execution']['quality_annotations_verified'])

    def test_best_preference_uses_source_not_manual_ivs_or_hyper_training(self):
        value = duplicate_copies()
        value['annotations']['pokemon-00000']['ivs'] = dict.fromkeys(observations.STATS, 31)
        plan = collection.move_plan(value, reference_catalog.build_reference(), profile({'prefer': 'most_best'}), True)
        self.assertEqual(plan['target'][0], 'pokemon-00001')
        self.assertEqual(plan['collection_review']['extras'][0]['action'], 'keep_for_review')
        self.assertEqual(plan['collection_execution']['stationary_review_occurrences'], [])

    def test_zero_attack_and_speed_requirements_do_not_treat_ranges_as_exact(self):
        value = duplicate_copies()
        set_judge(value, 'pokemon-00000', ['Best', 'No Good', 'Best', 'Best', 'Best', 'No Good'])
        report = collection.review(value, reference_catalog.build_reference(), profile({'require': {'attack': 'No Good', 'speed': 'No Good'}}))
        self.assertEqual(report['assignments'][0]['occurrence'], 'pokemon-00000')
        set_judge(value, 'pokemon-00000', ['Best', 'Decent', 'Best', 'Best', 'Best', 'No Good'])
        report = collection.review(value, reference_catalog.build_reference(), profile({'require': {'attack': 'No Good'}}))
        self.assertIsNone(report['assignments'][0]['occurrence'])

    def test_unreadable_copies_remain_for_review_and_cannot_move(self):
        value = supported_workspace(); set_judge(value, 'pokemon-00000', ['Best'] * 6)
        report = collection.review(value, reference_catalog.build_reference(), profile({'prefer': 'most_best'}))
        self.assertEqual(report['counts']['assigned'], 1)
        plan = collection.move_plan(value, reference_catalog.build_reference(), profile({'prefer': 'most_best'}), True)
        self.assertIsNone(plan['collection_execution']['judge_requirements']['pokemon-00001']['hp'])
        p = profile({'prefer': 'most_best'}); p['collections'][0]['start_slot'] = 2
        with self.assertRaisesRegex(ValueError, 'all six'):
            collection.move_plan(value, reference_catalog.build_reference(), p, True)
        value['source_inventory']['pokemon']['pokemon-00000']['collection_observation'].update(judge={'status':'unknown'}, ivs=None)
        report = collection.review(value, reference_catalog.build_reference(), profile({'prefer': 'most_best'}))
        self.assertEqual(len(report['quality_unresolved']), 1)
        self.assertIsNone(report['assignments'][0]['occurrence'])
        self.assertEqual(report['counts']['review'], 2)

    def test_disagreeing_raw_readings_and_edited_ranges_are_refused(self):
        for defect in ('confirmation', 'exact'):
            value = duplicate_copies(); evidence = value['source_inventory']['pokemon']['pokemon-00001']['collection_observation']
            if defect == 'confirmation': evidence['judge']['stats']['hp']['raw_confirmation'] = '8est'
            else: evidence['ivs']['hp'] = 30
            with self.subTest(defect=defect), self.assertRaises(ValueError):
                execution_policy.build_execution_contract(value, verify_judge=True)

    def test_unknown_cannot_distinguish_otherwise_identical_copies(self):
        value = duplicate_copies()
        value['source_inventory']['pokemon']['pokemon-00000']['collection_observation'].update(judge={'status':'unknown'}, ivs=None)
        source = value['source_inventory']['slots']
        contract = execution_policy.build_execution_contract(value, source, {'assignments':[]}, verify_judge=True)
        self.assertEqual(contract['stationary_review_occurrences'], ['pokemon-00000', 'pokemon-00001'])
        with self.assertRaisesRegex(ValueError, 'identical observed'):
            execution_policy.build_execution_contract(value, source, {'assignments':[{'occurrence':'pokemon-00001'}]}, verify_judge=True)
        # Quality masks overlap despite different annotations and observed completeness.
        self.assertTrue(quality.overlap(quality.source_judge(value['source_inventory']['pokemon']['pokemon-00000']),
                                       quality.source_judge(value['source_inventory']['pokemon']['pokemon-00001'])))

    def test_manual_choice_overrides_ranking_but_must_meet_requirements(self):
        for policy in ({'prefer':'sum_iv'}, {'require':{'attack':31}}, {'require':{'unknown':'Best'}}):
            with self.subTest(policy=policy), self.assertRaises(ValueError): quality.validate_policy(policy)
        value = duplicate_copies(); p=profile({'prefer':'most_best'});p['collections'][0]['preferred']={'pichu':'pokemon-00000'}
        report = collection.review(value, reference_catalog.build_reference(), p)
        self.assertEqual(report['assignments'][0]['occurrence'], 'pokemon-00000')
        p['collections'][0]['quality']['require'] = {'hp':'Best'}
        with self.assertRaises(ValueError): collection.review(value, reference_catalog.build_reference(), p)


if __name__ == '__main__': unittest.main()

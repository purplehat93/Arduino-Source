import copy
import hashlib
import unittest

import collection
import execution_policy
import reference_catalog
from test_collection import workspace


def supported_workspace():
    result = workspace(30, 2)
    reference = reference_catalog.build_reference()
    digest = hashlib.sha256((reference_catalog.DATA / 'HomeCollectionReference.json').read_bytes()).hexdigest()
    for item, dex, name, primary, key in [
            ('pokemon-00000', 172, 'pichu', 'electric', 'pichu'),
            ('pokemon-00001', 1, 'bulbasaur', 'grass', 'bulbasaur')]:
        observed = result['source_inventory']['pokemon'][item]
        observed.update(dex=dex, name=name, primary_type=primary,
                        secondary_type='poison' if dex == 1 else 'none', gmax=False)
        match = reference_catalog.match_form(observed, reference)
        observed['collection_observation'] = {'version': 1, 'form': {
            **match, 'fields_confirmed': True, 'resource': 'Pokemon/HomeCollectionReference.json',
            'resource_sha256': digest}, 'judge': {'status': 'unknown'}, 'ivs': None}
        result['annotations'][item].update(form_key=key, identity_reviewed=True)
    return result


class ExecutionPolicyTests(unittest.TestCase):
    def test_reviewed_metadata_plan_conserves_source_and_has_live_contract(self):
        value = supported_workspace()
        original = copy.deepcopy(value)
        catalog = reference_catalog.build_reference()
        profiles = {'version': 1, 'collections': [{'id': 'pilot', 'kind': 'custom',
                    'form_keys': ['pichu'], 'start_box': 1, 'start_slot': 2}]}
        preview = collection.move_plan(value, catalog, profiles)
        self.assertTrue(preview['execution_blocked'])
        plan = collection.move_plan(value, catalog, profiles, enable_execution=True)
        self.assertFalse(plan['execution_blocked'])
        self.assertEqual(plan['collection_execution']['form_keys'],
                         {'pokemon-00000': 'pichu', 'pokemon-00001': 'bulbasaur'})
        self.assertEqual(plan['source_validation'], collection.planner.source_validation(value['source_inventory']))
        self.assertEqual(value, original)
        self.assertGreater(len(plan['moves']), 0)
        self.assertFalse(plan['collection_execution']['quality_annotations_verified'])

    def test_unsupported_or_inconsistent_source_evidence_is_refused(self):
        for defect in ('unreviewed', 'unconfirmed', 'visual', 'ambiguous', 'wrong_species', 'wrong_key', 'missing_hash'):
            value = supported_workspace()
            item = 'pokemon-00000'
            observed = value['source_inventory']['pokemon'][item]
            form = observed['collection_observation']['form']
            if defect == 'unreviewed': value['annotations'][item]['identity_reviewed'] = False
            if defect == 'unconfirmed': form['fields_confirmed'] = False
            if defect == 'visual': form['evidence'] = 'reference_fields_and_two_box_icons'
            if defect == 'ambiguous': form['candidates'].append('pichu-spiky-eared')
            if defect == 'wrong_species': observed['name'] = 'pikachu'
            if defect == 'wrong_key': value['annotations'][item]['form_key'] = 'pichu-spiky-eared'
            if defect == 'missing_hash': form.pop('resource_sha256')
            with self.subTest(defect=defect), self.assertRaisesRegex(ValueError, 'metadata form'):
                execution_policy.build_execution_contract(value)

    def test_indistinguishable_copies_are_refused_even_with_different_annotations(self):
        value = supported_workspace()
        first, second = 'pokemon-00000', 'pokemon-00001'
        value['source_inventory']['pokemon'][second] = copy.deepcopy(value['source_inventory']['pokemon'][first])
        value['annotations'][second] = copy.deepcopy(value['annotations'][first])
        value['annotations'][second]['note'] = 'Different manual note does not distinguish this copy.'
        with self.assertRaisesRegex(ValueError, 'identical observed copies'):
            execution_policy.build_execution_contract(value)

    def test_identical_extras_can_stay_untouched_but_cannot_be_displaced(self):
        value = supported_workspace()
        for index in (2, 3):
            item = f"pokemon-{index:05d}"
            value['source_inventory']['slots'][index] = item
            value['source_inventory']['pokemon'][item] = copy.deepcopy(value['source_inventory']['pokemon']['pokemon-00001'])
            value['annotations'][item] = copy.deepcopy(value['annotations']['pokemon-00001'])
        profile = {'version': 1, 'collections': [{'id': 'pilot', 'kind': 'custom',
                   'form_keys': ['pichu'], 'start_box': 1, 'start_slot': 5}]}
        plan = collection.move_plan(value, reference_catalog.build_reference(), profile, enable_execution=True)
        self.assertEqual(plan['collection_execution']['stationary_review_occurrences'],
                         ['pokemon-00001', 'pokemon-00002', 'pokemon-00003'])
        for index in (1, 2, 3):
            self.assertEqual(plan['target'][index], value['source_inventory']['slots'][index])
        profile['collections'][0]['start_slot'] = 2
        with self.assertRaisesRegex(ValueError, 'identical observed copies'):
            collection.move_plan(value, reference_catalog.build_reference(), profile, enable_execution=True)

    def test_historical_capture_is_rechecked_under_current_reference(self):
        value = supported_workspace()
        for observed in value['source_inventory']['pokemon'].values():
            observed['collection_observation']['form']['resource_sha256'] = '0' * 64
        contract = execution_policy.build_execution_contract(value)
        self.assertNotEqual(contract['reference_sha256'], '0' * 64)
        self.assertEqual(value['source_inventory']['pokemon']['pokemon-00000']['collection_observation']['form']['resource_sha256'], '0' * 64)

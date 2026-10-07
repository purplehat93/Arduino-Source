import copy
import unittest
import collection
import execution_policy
from test_execution_policy import supported_workspace

class SpeciesExecutionTests(unittest.TestCase):
    def test_unknown_forms_and_ivs_do_not_become_approvals(self):
        w=supported_workspace()
        for a in w['annotations'].values():a.update(form_key=None,identity_reviewed=False)
        for r in w['source_inventory']['pokemon'].values():
            r['collection_observation']['form'].update(key=None,status='unknown',candidates=['a','b'])
        before=copy.deepcopy(w);p=collection.species_move_plan(w)
        self.assertEqual(w,before)
        self.assertFalse(p['execution_blocked'])
        self.assertEqual(p['collection_execution']['mode'],'metadata_species_v2')
        self.assertFalse(p['collection_execution']['release_enabled'])
        self.assertEqual(p['source_validation'],collection.planner.source_validation(w['source_inventory']))
        self.assertGreater(len(p['moves']),0)
        with self.assertRaises(ValueError):execution_policy.build_execution_contract(w)
    def test_metadata_twins_move_and_protected_copies_stay_still(self):
        w=supported_workspace()
        w['source_inventory']['slots'][2]='twin'
        w['source_inventory']['pokemon']['twin']=copy.deepcopy(w['source_inventory']['pokemon']['pokemon-00001'])
        w['annotations']['twin']=copy.deepcopy(w['annotations']['pokemon-00001'])
        p=collection.species_move_plan(w)
        self.assertEqual(p['target'][:3],['pokemon-00001','twin','pokemon-00000'])
        self.assertEqual(p['collection_execution']['stationary_review_occurrences'],[])
        w=supported_workspace();w['annotations']['pokemon-00000']['protected']=True
        p=collection.species_move_plan(w);self.assertEqual(p['target'][0],'pokemon-00000')
    def test_unconfirmed_copies_stay_still_and_wrong_species_is_rejected(self):
        w=supported_workspace();w['source_inventory']['pokemon']['pokemon-00000']['collection_observation']['form']['fields_confirmed']=False
        p=collection.species_move_plan(w);self.assertEqual(p['target'][0],'pokemon-00000')
        w=supported_workspace();w['source_inventory']['pokemon']['pokemon-00000']['name']='pikachu'
        with self.assertRaises(ValueError):collection.species_move_plan(w)

    def test_completely_full_boxes_sort_twins_without_spare_slots(self):
        import random
        for seed in range(12):
            w=supported_workspace(); templates=copy.deepcopy(w['source_inventory']['pokemon'])
            annotations=copy.deepcopy(w['annotations'])
            w['source_inventory']['pokemon']={}; w['annotations']={}
            ids=[]
            for i in range(30):
                item=f'copy-{i:02d}'; template=f'pokemon-{i%2:05d}'
                ids.append(item)
                w['source_inventory']['pokemon'][item]=copy.deepcopy(templates[template])
                w['annotations'][item]=copy.deepcopy(annotations[template])
            random.Random(seed).shuffle(ids);w['source_inventory']['slots']=ids
            p=collection.species_move_plan(w)
            self.assertEqual(set(p['target']),set(ids))
            self.assertNotIn(None,p['target'])
            self.assertEqual([p['inventory']['pokemon'][i]['dex'] for i in p['target']],[1]*15+[172]*15)
            state=ids.copy()
            for move in p['moves']:
                a,b=move['source'],move['destination']
                first=execution_policy.species_record(p['inventory']['pokemon'][state[a]],False)
                second=execution_policy.species_record(p['inventory']['pokemon'][state[b]],False)
                self.assertNotEqual(first,second)
                state[a],state[b]=state[b],state[a]
            self.assertEqual(state,p['target'])

if __name__=='__main__':unittest.main()

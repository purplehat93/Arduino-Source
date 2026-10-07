"""Source-bound HOME Judge evidence and explicit copy preferences."""
import observations


def parse_rating(raw):
    if not isinstance(raw, str) or any(not ('a' <= c.lower() <= 'z' or c in ' \r\n\t!') for c in raw):
        return None
    key = ''.join(c.lower() for c in raw if c.isalpha())
    return next((label for label in observations.RATINGS
                 if ''.join(c.lower() for c in label if c.isalpha()) == key), None)


def source_judge(observed):
    evidence = observed.get('collection_observation', {})
    judge = evidence.get('judge', {'status': 'unknown'})
    exact = observations.validate_judge(judge)
    if evidence.get('ivs') != exact:
        raise ValueError('Recorded exact IVs disagree with source Judge evidence.')
    result = dict.fromkeys(observations.STATS)
    for stat, record in judge.get('stats', {}).items():
        if record['status'] != 'read':
            continue
        if any(parse_rating(record[key]) != record['rating'] for key in ('raw', 'raw_confirmation')):
            raise ValueError('Judge quality requires agreeing independent source readings.')
        result[stat] = {key: record[key] for key in ('rating', 'iv_min', 'iv_max', 'hyper_trained')}
    return result


def validate_policy(policy):
    if not isinstance(policy, dict) or set(policy) - {'require', 'prefer'}:
        raise ValueError('Quality policy supports require and prefer only.')
    required = policy.get('require', {})
    if (not isinstance(required, dict) or set(required) - set(observations.STATS)
            or any(value not in observations.RATINGS for value in required.values())):
        raise ValueError('Quality requirements need stat names and exact Judge labels.')
    if policy.get('prefer', 'existing') not in ('existing', 'most_best'):
        raise ValueError('Quality preference must be existing or most_best.')
    return required, policy.get('prefer', 'existing')


def overlap(first, second):
    """Unknown stats cannot distinguish two otherwise identical copies."""
    return all(first[stat] is None or second[stat] is None or first[stat] == second[stat]
               for stat in observations.STATS)

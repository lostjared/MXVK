"""Local top-ten high scores with atomic replacement."""

import json
from pathlib import Path


class Scores:
    def __init__(self, path):
        self.path = Path(path)
        self.entries = []
        self.error = ''
        try:
            data = json.loads(self.path.read_text())
            if isinstance(data, list):
                self.entries = sorted([(str(row['name'])[:16], max(0, int(row['score']))) for row in data if isinstance(row, dict)], key=lambda row: row[1], reverse=True)[:10]
        except (OSError, ValueError, KeyError, TypeError):
            pass

    def qualifies(self, score):
        return score > 0 and (len(self.entries) < 10 or score > self.entries[-1][1])

    def add(self, name, score):
        entries = sorted(self.entries + [(name.strip()[:16] or 'PLAYER', score)], key=lambda row: row[1], reverse=True)[:10]
        try:
            temporary = self.path.with_suffix('.tmp')
            temporary.write_text(json.dumps([{'name': name, 'score': score} for name, score in entries], indent=2) + '\n')
            temporary.replace(self.path)
        except OSError as error:
            self.error = f'Could not save scores: {error}'
            return False
        self.entries = entries
        self.error = ''
        return True

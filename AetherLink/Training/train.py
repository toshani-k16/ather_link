import csv
import json
import math
from collections import defaultdict

def load_data(path="training_data.csv"):
    data = []
    with open(path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            data.append((row["text"], row["label"]))
    return data

def tokenize(text):
    text = text.lower()
    cleaned = "".join(c if c.isalnum() else " " for c in text)
    return cleaned.split()

class NaiveBayesClassifier:
    def __init__(self):
        self.class_word_counts = defaultdict(lambda: defaultdict(int)) 
        self.class_totals = defaultdict(int)      
        self.class_doc_counts = defaultdict(int)   
        self.vocab = set()
        self.total_docs = 0

    def train(self, data):
        for text, label in data:
            tokens = tokenize(text)
            self.class_doc_counts[label] += 1
            self.total_docs += 1
            for word in tokens:
                self.class_word_counts[label][word] += 1
                self.class_totals[label] += 1
                self.vocab.add(word)

    def predict(self, tokens):
        vocab_size = len(self.vocab)
        best_label = None
        best_score = float("-inf")

        for label in self.class_doc_counts:
            prior = math.log(self.class_doc_counts[label] / self.total_docs)
            score = prior

            for word in tokens:
                word_count = self.class_word_counts[label].get(word, 0)
                total = self.class_totals[label]
                prob = (word_count + 1) / (total + vocab_size)
                score += math.log(prob)

            if score > best_score:
                best_score = score
                best_label = label

        return best_label, best_score

    def save(self, path="model.json"):
        export = {
            "vocab": list(self.vocab),
            "class_doc_counts": dict(self.class_doc_counts),
            "class_totals": dict(self.class_totals),
            "total_docs": self.total_docs,
            "class_word_counts": {
                label: dict(words) for label, words in self.class_word_counts.items()
            },
        }
        with open(path, "w", encoding="utf-8") as f:
            json.dump(export, f, indent=2)


if __name__ == "__main__":
    data = load_data("training_data.csv")
    print(f"Loaded {len(data)} training examples")

    clf = NaiveBayesClassifier()
    clf.train(data)

    print(f"Vocab size: {len(clf.vocab)}")
    print(f"Categories: {list(clf.class_doc_counts.keys())}")

    test_sentences = [
        "my grandmother needs insulin immediately at 5th floor",
        "we need drinking water urgently",
        "help fire spreading fast near building B",
    ]
    for s in test_sentences:
        label, score = clf.predict(tokenize(s))
        print(f"{s!r} -> {label} (score={score:.3f})")

    clf.save("model.json")
    print("Saved model.json")
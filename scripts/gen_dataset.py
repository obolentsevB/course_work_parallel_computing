#!/usr/bin/env python3

import argparse
import os
import random

TOPICS = {
    "space": "space galaxy star planet orbit rocket astronaut telescope universe cosmos nebula comet moon mars alien spaceship",
    "film": "film movie cinema actor actress director scene plot screenplay camera premiere audience comedy drama thriller",
    "music": "music song album guitar piano concert singer melody rhythm band orchestra jazz rock lyrics drummer",
    "food": "food recipe kitchen chef dinner bread cheese pasta soup spice flavor dessert restaurant breakfast vegetable",
    "sport": "football soccer basketball tennis match team coach player goal tournament stadium season champion league score",
    "tech": "computer software algorithm processor network server database program code thread parallel memory cluster cloud internet",
    "nature": "forest river mountain ocean tree flower animal bird wolf weather rain snow island desert valley",
    "history": "history empire king queen war battle castle ancient revolution century kingdom army treaty medieval ruins",
    "health": "health doctor hospital medicine patient disease vaccine therapy exercise sleep diet nurse clinic heart brain",
    "travel": "travel journey trip hotel airport flight passport tourist city museum beach train map adventure luggage",
}
COMMON = ("people time year day way world life hand part place case week company system "
          "group number night point home water room mother area money story fact month "
          "lot right study book eye job word business issue side kind head house service "
          "friend father power hour game line end member law car name president team "
          "good new first last long great little own other old big high different small "
          "large next early young important few public bad same able make know take see "
          "come think look want give use find tell ask work seem feel try leave call keep "
          "really very also often always never still well even again almost together").split()
TEMPLATES = [
    "The {t1} and the {c1} {v} a {c2} {t2}.",
    "Every {c1} we {v} about the {t1} near the {t2}.",
    "My {c1} said the {t1} was {c2} but the {t2} was better.",
    "In this {c1} the {t1} {v} with a {t2} and some {t3}.",
    "Nobody {v} how the {t1} could become a {t2} so quickly.",
    "A {c1} {t1} is the reason why {c2} {t2} matters.",
]
VERBS = "talk think write dream learn argue read hear care worry".split()


def sentence(rng, topic_words):
    pick = lambda: rng.choice(topic_words) if rng.random() < 0.6 else rng.choice(COMMON)
    return rng.choice(TEMPLATES).format(
        t1=pick(), t2=pick(), t3=pick(), c1=rng.choice(COMMON), c2=rng.choice(COMMON),
        v=rng.choice(VERBS))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--out", default="data/generated")
    p.add_argument("--size-mb", type=int, default=2100, help="total size of the dataset")
    p.add_argument("--doc-kb", type=int, default=20, help="average document size")
    p.add_argument("--seed", type=int, default=42)
    args = p.parse_args()

    rng = random.Random(args.seed)
    topics = {k: v.split() for k, v in TOPICS.items()}
    names = list(topics)
    # A pool of pre-generated sentences per topic keeps generation fast.
    pool = {k: [sentence(rng, w) for _ in range(20000)] for k, w in topics.items()}

    target = args.size_mb * 1024 * 1024
    written, doc = 0, 0
    while written < target:
        chosen = rng.sample(names, rng.choice([1, 2]))
        size = int(args.doc_kb * 1024 * rng.uniform(0.3, 1.7))
        parts, length = [], 0
        while length < size:
            s = rng.choice(pool[rng.choice(chosen)])
            parts.append(s)
            length += len(s) + 1
        folder = os.path.join(args.out, f"{doc // 1000:04d}")
        os.makedirs(folder, exist_ok=True)
        with open(os.path.join(folder, f"doc_{doc:07d}.txt"), "w") as f:
            f.write(" ".join(parts))
        written += length
        doc += 1
        if doc % 5000 == 0:
            print(f"{doc} documents, {written / 2**20:.0f} MB")
    print(f"done: {doc} documents, {written / 2**20:.0f} MB in {args.out}")


if __name__ == "__main__":
    main()
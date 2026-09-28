import itertools
import random
import csv

CATEGORIES = ["MEDICAL", "FOOD", "WATER", "SHELTER", "RESCUE", "FIRE", "SECURITY"]

people = ["my grandmother", "my son", "an old man", "a child", "my neighbor", "we", "a pregnant woman", "an injured firefighter", "an infant", "a diabetic patient", "an elderly couple",
    "a person in a wheelchair", "my daughter", "my husband", "my wife", "several trapped workers",
    "an unconscious person", "a family of four", "a blind person", "an asthmatic person", "a newborn",
    "my mother", "my father", "a group of seniors", "a paralyzed man", "a stroke victim", "a diabetic child", "a pregnant teen", "my grandfather", "my nephew",
    "an injured officer", "a group of children", "an bedridden patient", "a hemophiliac patient", "a toddler",
    "my coworker", "a visually impaired person", "a hearing impaired person", "a family with a baby", "an amputee",
    "a heatstroke victim", "several stranded tourists", "an elderly man", "a nursing mother"]

needs = {
    "MEDICAL":  ["an EpiPen", "epinephrine", "insulin injections", "a blood pressure monitor", "antiseptic wipes", "gauze pads", "a wrist splint", "saline solution", "morphine", "a pulse oximeter", "cold compresses", "a catheter", "anti-allergy medication", "insulin", "an inhaler", "a bandage", "medicine", "a doctor", "an epipen", "first aid", "a tourniquet", "an ambulance", "oxygen", "a ventilator", "a blood transfusion", "burn ointment", "asthma medication", "stitches", "a stretcher", "painkillers", "antibiotics", "a defibrillator", "a neck brace", "dialysis treatment"],
    "FOOD":     ["baby meal powder", "protein bars", "canned soup", "peanut butter", "canned beans", "condensed milk", "energy gels", "dehydrated meals", "rice bags", "lentils", "canned tuna", "food", "baby formula", "rice", "clean food supplies", "canned goods", "ration packets", "high-calorie food", "uncontaminated food", "MREs", "dry snacks", "bread", "cereal", "infant food", "non-perishable food", "energy bars", "formula milk"],
    "WATER":    ["clean water jugs", "rehydration powder", "disinfectant tablets", "filtered water pouches", "electrolyte solution", "drinking water barrels", "clean boiling water", "drinking water", "clean water", "a water filter", "bottled water", "potable water", "water purification tablets", "gallons of water", "hydration salts", "clean drinking water", "boiling water", "a water container"],
    "SHELTER":  ["thermal blankets", "a pop-up tent", "heavy jackets", "waterproof tarpaulins", "a canopy shelter", "foam sleeping pads", "thick blankets", "a kerosene heater", "winter coats", "a tent", "blankets", "warm clothes", "temporary shelter", "tarps", "a dry place to stay", "emergency blankets", "a sleeping bag", "a waterproof cover", "a heater", "dry clothes", "raincoats", "a safe building"],
    "RESCUE":   ["a sturdy ladder", "a heavy-duty rope", "a crowbar", "a signal flare", "a megawatt megaphone", "safety goggles", "a thermal imaging device", "a rescue net", "hydraulic cutters","rescue", "someone to pull us out", "a rope", "a ladder", "a rescue boat", "a helicopter", "heavy machinery", "a search team", "a life vest", "rescue equipment", "a winch", "bolt cutters", "a flashlight", "a whistle", "a harness"],
    "FIRE":     ["a carbon monoxide alarm", "a fire suppression blanket", "a CO2 extinguisher", "heat-resistant gloves", "smoke masks", "an emergency fire axe", "a portable water pump", "a fire extinguisher", "firefighters", "help putting out a fire", "smoke ventilation", "a fire hose", "firefighters", "a fire blanket", "gas valve shutdown", "burn protection", "a ladder truck"],
    "SECURITY": ["armed security", "police backup", "a lockable room", "a safe perimeter", "crowd dispersion units", "emergency escort", "physical protection", "police", "protection", "help, someone is threatening us", "police officers", "security guards", "a safe zone", "protection from looters", "an escort out of the area", "crowd control", "a secure room"],
}

locations = ["5th floor", "building B", "near the bridge", "room 12", "the parking lot", "the rooftop", "the basement", "block 4 apartment 201", "near the city hall", "exit 3",
    "the main intersection", "the collapsed stairwell", "behind the school", "sector 7", "the attic",
    "by the riverbank", "highway 101", "the subway station", "floor 3", "near the gas station",
    "the sports complex", "the back alley", "the underground parking garage", "apartment 5B", "the emergency entrance", "the south gate", "the west wing",
    "near the water tower", "the central plaza", "the elevated highway", "under the overpass", "the train station platform",
    "the warehouse district", "terminal 2", "the community center", "the elementary school gym", "the rooftop helipad",
    "sector 4 loading dock", "the basement shelter"]

templates = [
    "{person} needs {need} immediately, we are at {location}",
    "help, {need} required urgently for {person} at {location}",
    "{person} is trapped and needs {need}, please send help to {location}",
    "urgent: {need} needed at {location} for {person}",
    "SOS: {person} at {location} desperately needs {need}",
    "can anyone bring {need} to {location}? {person} is in bad condition",
    "emergency at {location}, {person} is asking for {need}",
    "we are stuck at {location}. {person} won't survive without {need}",
    "PLEASE HELP! {need} is required at {location} for {person}",
    "{location} - {person} requires {need} ASAP",
    "does anyone have {need}? {person} is at {location} and can't move",
    "critical situation at {location}, {person} needs {need} right now",
    "send {need} to {location} for {person} as soon as possible",
    "can someone drop off {need} at {location}? {person} is waiting",
    "{person} located at {location} has no access to {need}",
    "we desperately require {need} for {person} situated at {location}",
    "URGENT: {person} at {location} is severely in need of {need}",
    "can someone check on {location}? {person} needs {need} immediately",
    "CRITICAL: request for {need} at {location} for {person}",
    "we have {person} at {location} who desperately requires {need}",
    "attention responders: deliver {need} to {location} for {person}",
    "is there any way to get {need} to {person} located at {location}?",
    "{location} update: {person} is stable but urgently needs {need}",
    "MAYDAY: {person} stuck at {location}, need {need} as soon as possible",
]

def generate_examples(category, n=1000000000000000000000):
    combos = list(itertools.product(templates, people, needs[category], locations))
    random.shuffle(combos)
    examples = []
    for template, person, need, location in combos[:n]:
        sentence = template.format(person=person, need=need, location=location)
        examples.append((sentence, category))
    return examples

def build_dataset():
    all_data = []
    for category in CATEGORIES:
        all_data += generate_examples(category, n=1000000000000000000000)
    random.shuffle(all_data)
    return all_data

def save_csv(data, path="training_data.csv"):
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["text", "label"])
        writer.writerows(data)

if __name__ == "__main__":
    data = build_dataset()
    print(f"Total examples: {len(data)}")
    for sentence, label in data[:10]:
        print(f"{label:10s} | {sentence}")
    save_csv(data)
    print("Saved to training_data.csv")
#!/usr/bin/env python3
"""rdf_to_graph 테스트용 가짜 rdfstore.db 생성기.

main.cpp / rdf_graph_generator.cpp 가 읽는 테이블 구조(kb_*_type/asserted/literal_statements)를
그대로 맞춘다. 토폴로지는 단순하게:
  Feeder -[KEPCOFeeder.CircuitBreaker]-> CB - Line - Switch - Line - Switch ... (피더마다 직렬)
  피더 i 의 끝과 피더 i+1 의 끝을 상시연계점(Switch.open=1)으로 연결.

usage: python3 gen_synthetic_db.py out.db --feeders 100 --units 150
"""
import argparse, os, sqlite3

EXT = "http://iec.ch/TC57/1999/rdf-schema-extensions-19990926#"
BASE = "http://www.desktop-eqkvgq5.net/2019/cim#"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--feeders", type=int, default=100)
    ap.add_argument("--units", type=int, default=150, help="피더당 (선로+스위치) 쌍 개수")
    a = ap.parse_args()

    if os.path.exists(a.out):
        os.remove(a.out)
    db = sqlite3.connect(a.out)
    p = "kb_0000000000"
    db.executescript(f"""
    CREATE TABLE {p}_type_statements (id INTEGER PRIMARY KEY, member TEXT, klass TEXT, context TEXT, termcomb INTEGER);
    CREATE TABLE {p}_asserted_statements (id INTEGER PRIMARY KEY, subject TEXT, predicate TEXT, object TEXT, context TEXT, termcomb INTEGER);
    CREATE TABLE {p}_literal_statements (id INTEGER PRIMARY KEY, subject TEXT, predicate TEXT, object TEXT, context TEXT, termcomb INTEGER, objlanguage VARCHAR, objdatatype VARCHAR);
    """)
    T, A, L = [], [], []
    ctx = "synthetic"
    seq = [10000000000000]

    def uri():
        seq[0] += 1
        return f"{BASE}_{seq[0]}"

    def typ(u, k):
        T.append((u, EXT + k, ctx, 0))

    def named(u, name):
        # 실데이터처럼 이름이 거의 unique 하도록 id 를 붙임
        name = f"{name}_{u.rsplit('_', 1)[1]}"
        L.append((u, EXT + "IdentifiedObject.name", name, ctx, 0, None, None))
        L.append((u, EXT + "IdentifiedObject.aliasName", name, ctx, 0, None, None))

    def equip(kind, name, open_val=None):
        u = uri(); typ(u, kind); named(u, name)
        if open_val is not None:
            L.append((u, EXT + "Switch.open", str(open_val), ctx, 0, None, None))
            L.append((u, EXT + "Switch.normalOpen", str(open_val), ctx, 0, None, None))
        return u

    def terminal(ceq, cn):
        t = uri(); typ(t, "Terminal"); named(t, "T")
        A.append((t, EXT + "Terminal.ConductingEquipment", ceq, ctx, 0))
        A.append((t, EXT + "Terminal.ConnectivityNode", cn, ctx, 0))

    def new_cn():
        c = uri(); typ(c, "ConnectivityNode"); named(c, "CN")
        return c

    def connect(e1, e2):
        cn = new_cn(); terminal(e1, cn); terminal(e2, cn)

    ends = []
    first_switch = None
    for f in range(a.feeders):
        feeder = equip("KEPCOFeeder", f"F{f}")
        cb = equip("CircuitBreaker", f"CB{f}", 0)
        A.append((feeder, EXT + "KEPCOFeeder.CircuitBreaker", cb, ctx, 0))
        prev = cb
        for i in range(a.units):
            line = equip("ACLineSegment", f"L{f}_{i}")
            connect(prev, line)
            sw = equip("ManualSwitch", f"S{f}_{i}", 0)
            if first_switch is None:
                first_switch = sw
            connect(line, sw)
            prev = sw
        ends.append(prev)
    for f in range(a.feeders - 1):
        tie = equip("ManualSwitch", f"TIE{f}", 1)
        connect(ends[f], tie)
        connect(tie, ends[f + 1])

    db.executemany(f"INSERT INTO {p}_type_statements (member,klass,context,termcomb) VALUES (?,?,?,?)", T)
    db.executemany(f"INSERT INTO {p}_asserted_statements (subject,predicate,object,context,termcomb) VALUES (?,?,?,?,?)", A)
    db.executemany(f"INSERT INTO {p}_literal_statements (subject,predicate,object,context,termcomb,objlanguage,objdatatype) VALUES (?,?,?,?,?,?,?)", L)
    db.commit(); db.close()
    print(f"triples: type={len(T)} asserted={len(A)} literal={len(L)} total={len(T)+len(A)+len(L)}")
    print("sample switch_id:", first_switch.rsplit('#', 1)[1])

if __name__ == "__main__":
    main()

// nep-db.js — een nagebouwde `claude.use('db')` voor tools/headless/tool.mjs (via addInitScript), zodat meekijken en
// voorstellen zonder claude.ai te toetsen zijn. Zelfde vorm als de echte (collection/doc, onSnapshot, get/set/update/delete,
// snapshots asynchroon); de toets leest en schrijft via globalThis.__nepDb, zoals Claude dat via de database zou doen.
(() => {
  const store = new Map();          // 'coll/id' -> data
  const luisteraars = new Map();    // coll -> Set(next)
  const docs = (coll) => [...store].filter(([p]) => p.startsWith(coll + '/') && p.split('/').length === 2).map(([p, d]) => ({ id: p.split('/')[1], exists: true, data: () => JSON.parse(JSON.stringify(d)), metadata: { fromCache: false, hasPendingWrites: false } }));
  const meld = (coll) => setTimeout(() => { for (const f of luisteraars.get(coll) || []) f({ docs: docs(coll), size: docs(coll).length, empty: !docs(coll).length, docChanges: () => [], metadata: {} }); }, 0);
  const schrijf = (pad, data) => { store.set(pad, JSON.parse(JSON.stringify(data))); meld(pad.split('/')[0]); };
  const doc = (pad) => ({
    id: pad.split('/')[1], path: pad,
    get: async () => (store.has(pad) ? { id: pad.split('/')[1], exists: true, data: () => JSON.parse(JSON.stringify(store.get(pad))) } : { id: pad.split('/')[1], exists: false, data: () => undefined }),
    set: async (d) => { schrijf(pad, d); },
    update: async (d) => { if (!store.has(pad)) throw { code: 'invalid_argument', message: 'bestaat niet' }; schrijf(pad, { ...store.get(pad), ...d }); },
    delete: async () => { store.delete(pad); meld(pad.split('/')[0]); },
  });
  const db = {
    doc,
    collection: (coll) => ({ path: coll, doc: (id) => doc(coll + '/' + id),
      onSnapshot(next) { if (!luisteraars.has(coll)) luisteraars.set(coll, new Set()); luisteraars.get(coll).add(next); meld(coll); return () => luisteraars.get(coll).delete(next); } }),
  };
  globalThis.__nepDb = { store, schrijf, schrijven: [] };
  globalThis.claude = { use: async (naam) => (naam === 'db' ? db : null) };
})();

// Tiny event bus shared by every Atlas module.
export function createBus() {
  const map = new Map();
  return {
    on(type, fn) {
      let set = map.get(type);
      if (!set) map.set(type, (set = new Set()));
      set.add(fn);
      return () => set.delete(fn);
    },
    emit(type, detail) {
      const set = map.get(type);
      if (!set) return;
      for (const fn of [...set]) {
        try { fn(detail, type); } catch (e) { console.error(`[bus] ${type} listener failed`, e); }
      }
    },
  };
}

export const bus = createBus();

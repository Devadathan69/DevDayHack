import { NpcController } from "./npc-controller.js";
import { validateProfile } from "./domain.js";

export class NpcRegistry {
  #profiles = new Map();
  #controllers = new Map();

  constructor(profiles, sharedDependencies) {
    this.sharedDependencies = sharedDependencies;
    for (const input of profiles) {
      const profile = validateProfile(input);
      if (this.#profiles.has(profile.id)) throw new Error(`Duplicate NPC profile id '${profile.id}'.`);
      this.#profiles.set(profile.id, profile);
    }
  }

  getProfile(id) {
    return this.#profiles.get(id);
  }

  registerProfile(input) {
    const existing = this.#profiles.get(input.id);
    if (existing) return existing;
    const profile = validateProfile(input);
    this.#profiles.set(profile.id, profile);
    return profile;
  }

  replaceProfile(input) {
    const profile = validateProfile(input);
    this.#profiles.set(profile.id, profile);
    this.#controllers.delete(profile.id);
    return profile;
  }

  get(id) {
    const profile = this.#profiles.get(id);
    if (!profile) throw new Error(`NPC '${id}' is not configured.`);
    if (!this.#controllers.has(id)) {
      this.#controllers.set(id, new NpcController(profile, this.sharedDependencies));
    }
    return this.#controllers.get(id);
  }

  get size() {
    return this.#profiles.size;
  }
}

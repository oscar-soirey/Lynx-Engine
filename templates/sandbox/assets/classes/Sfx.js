// Sounds : Sfx.play("jump") for a one-shot sound (sounds/jump.wav),
// Sfx.play("explosion", position) for a sound placed in the world (quieter
// when far from the listener), Sfx.music("music_chip") for a looping music.
//
// Each one-shot sound is a small actor with a SoundSource that destroys
// itself once the sound is over. The player sets Sfx.listener = this.

class SfxVoice extends Actor {
    constructor() {
        super();
        this.source = this.addComponent("SoundSource", { spatial: false, loop: false });
        this.left = 3.0;
    }

    Setup(name, volume, pitchVariation, duration) {
        this.source.sound = "sounds/" + name + ".wav";
        this.source.volume = volume;
        this.source.pitchVariation = pitchVariation;
        this.source.play();
        this.left = duration;
    }

    Update(dt) {
        this.left -= dt;
        if (this.left <= 0)
            this.destroy();
    }
}

class MusicPlayer extends Actor {
    static properties = { sound: "", volume: 0.5 };

    constructor() {
        super();
        this.source = this.addComponent("SoundSource", { spatial: false, loop: true });
    }

    Start(name, volume) {
        this.sound = name;
        this.source.sound = "sounds/" + name + ".wav";
        this.source.loop = true;
        this.source.volume = volume;
        this.source.play();
    }
}

class Sfx {
    static listener = null;
    static volume = 1.0;          // master volume of the effects
    static hearing = 40.0;        // distance (world units) where a sound is no longer heard
    static last = {};             // name -> time, to avoid 10 times the same sound in one frame
    static current = null;        // the MusicPlayer
    static warned = false;

    static play(name, at, opts) {
        opts = opts || {};
        let volume = (opts.volume !== undefined ? opts.volume : 0.8) * Sfx.volume;

        if (at && Sfx.listener && Sfx.listener.valid) {
            const dx = at.x - Sfx.listener.position.x;
            const dy = at.y - Sfx.listener.position.y;
            const d = Math.sqrt(dx * dx + dy * dy);
            volume *= Math.max(0, 1 - d / Sfx.hearing);
        }
        if (volume <= 0.02)
            return null;

        const now = Date.now();
        if (Sfx.last[name] && now - Sfx.last[name] < (opts.gap || 40))
            return null;
        Sfx.last[name] = now;

        const p = at || (Sfx.listener && Sfx.listener.valid ? Sfx.listener.position : vec3(0, 0, 0));
        const voice = Level.spawn("SfxVoice", vec3(p.x, p.y, 0));
        if (!voice) {
            Sfx.warn("Level.spawn(\"SfxVoice\") a echoue (classe Sfx.js non chargee ?)");
            return null;
        }
        voice.Setup(name, volume, opts.pitchVariation !== undefined ? opts.pitchVariation : 0.08, opts.duration || 3.0);
        return voice;
    }

    // Starts a looping music / ambience (nothing if it is already playing).
    static music(name, volume) {
        if (Sfx.current && Sfx.current.valid) {
            if (Sfx.current.sound === name)
                return;
            Sfx.current.destroy();
        }
        Sfx.current = Level.spawn("MusicPlayer", vec3(0, 0, 0));
        if (!Sfx.current)
            Sfx.warn("Level.spawn(\"MusicPlayer\") a echoue");
        else
            Sfx.current.Start(name, volume !== undefined ? volume : 0.45);
    }

    static warn(text) {
        if (Sfx.warned)
            return;
        Sfx.warned = true;
        print("[Sfx] " + text);
    }

    static stopMusic() {
        if (Sfx.current && Sfx.current.valid)
            Sfx.current.destroy();
        Sfx.current = null;
    }
}
globalThis.Sfx = Sfx;

/*
 * Infra Arcana page: windows (../rvip-wm.js), saves and app buttons (../rvip-app.js).
 * The game (src/web.cpp) sends every text window's content: status, prompt line,
 * messages, inventory, visible; the Map window holds the game's SDL canvas, sized
 * by the game to the window (web_resize). This file only places and persists.
 */
'use strict';
(function () {
	const $ = id => document.getElementById(id);
	const LAYOUT = () => RvipApp.dir + '/web-layout.json';
	const SAVE = () => RvipApp.dir + '/save';
	let wm = null, L = {}, tileNames = [], tilesMode = true, cv = [0, 0], face = '', invCache = '';

	/* ---------- windows ---------- */
	const WINS = [{ id: 'map', title: 'Map' }, { id: 'status', title: 'Status' }, { id: 'msg', title: 'Log messages' },
		{ id: 'inv', title: 'Inventory' }, { id: 'vis', title: 'Visible' }];
	const MULTI = { d: 'v', r: 0.8, a: { d: 'h', r: 0.72, a: 'map', b: { d: 'v', r: 0.42, a: 'status', b: { d: 'v', r: 0.6, a: 'inv', b: 'vis' } } }, b: 'msg' };
	/* one-window mode: the game's own full screen (its status and log drawn nowhere, so the side windows stay out) */
	const SINGLE = 'map';

	function saveLayout() {
		if (!wm) return;
		L.wm = wm.state();
		try { Module.FS.writeFile(LAYOUT(), JSON.stringify(L)); app.sync(); } catch (e) { }
	}
	/* the game draws its screen at the Map body's size (not below its minimum: then the body scrolls, centred on the player) */
	function fitMap() {
		const b = $('map'), c = $('canvas');
		if (!b.clientWidth) return;
		if (app.running && Module._web_resize) Module._web_resize(b.clientWidth, b.clientHeight);
		placeCanvas();
		placePop();
	}
	/* pop-up text: the game's grid as sent (colours from the game), centred on the map, text size = Log messages */
	function placePop() {
		const p = $('pop');
		if (p.hidden) return;
		p.style.fontSize = RvipWM.fontSize('msg') + 'px';
		RvipWM.popup(p, { center: true });
	}
	function showPop(s) {
		const p = $('pop'), same = !p.hidden && !!s;
		p.innerHTML = s; p.hidden = !s;
		if (!s) return;
		const st = p.scrollTop;
		placePop();
		if (same) p.scrollTop = st;
	}
	function placeCanvas() {
		const b = $('map'), c = $('canvas');
		if (!cv[0]) return;
		c.style.width = cv[0] + 'px'; c.style.height = cv[1] + 'px';
		/* the game keeps the player in the middle of its map */
		RvipWM.center(c, cv[0] / 2, cv[1] / 2, cv[0], cv[1], b.clientWidth, b.clientHeight);
	}
	function applyFonts() {
		const f = face ? '"' + face + '", ui-monospace, monospace' : '';
		for (const id of ['status-body', 'msg', 'inv', 'vis']) $(id).style.fontFamily = f;
		const t = document.querySelector('#t-map .wm-topl'); if (t) t.style.fontFamily = f;
	}
	function loadFace(n) {
		if (!n) { applyFonts(); return; }
		const ff = new FontFace(n, 'url(../fonts/' + n + '.woff)');
		ff.load().then(() => { document.fonts.add(ff); applyFonts(); }).catch(() => app.status('Could not load the font ' + n + '.', true));
	}
	function setupWM() {
		wm = RvipWM({
			area: $('game'), menu: $('btn-layout'), wins: WINS, multi: MULTI, single: SINGLE,
			state: L.wm || null,
			save: () => saveLayout(),
			layout: () => fitMap(),
			/* Map A-/A+: the game's whole-number video scale (1..4), nearest-neighbour */
			zoom: { map: (px, d) => { L.scale = Math.max(1, Math.min(4, (L.scale || 1) + d)); if (Module._web_set_scale) Module._web_set_scale(L.scale); saveLayout(); } },
			onReset: () => { L.scale = 1; if (Module._web_set_scale) Module._web_set_scale(1); applyFonts(); fitMap(); saveLayout(); },
		});
		applyFonts();
		wm.apply();
	}

	/* ---------- lists from the game ---------- */
	/* icon: the game's own tile (gfx/tiles/20x20, grey) masked over the row's colour, as the game tints it */
	function icon(t) {
		const n = tileNames[t];
		if (!tilesMode || !n) return null;
		const i = document.createElement('i');
		i.className = 'ic';
		i.style.setProperty('--m', 'url("gfx/tiles/20x20/' + n + '")');
		return i;
	}
	/* lines "=Header" or "css \t glyph \t tile \t text" */
	function showInv(s) {
		if (s === undefined) s = invCache; else invCache = s;
		const b = $('inv'); b.textContent = '';
		for (const l of s.split('\n')) {
			if (!l) continue;
			const d = document.createElement('div');
			if (l[0] === '=') { d.className = 'h'; d.textContent = l.slice(1); b.appendChild(d); continue; }
			const [col, g, tile, t] = l.split('\t');
			d.className = 'row'; d.style.color = col;
			const ic = tile !== '' && icon(+tile);
			if (ic) d.appendChild(ic);
			else if (g) { const e = document.createElement('b'); e.textContent = g; d.appendChild(e); }
			d.appendChild(document.createTextNode(t));
			b.appendChild(d);
		}
	}
	let visCache = '';
	function showVis(s) { if (s === undefined) { s = visCache; $('vis')._vis = null; } else visCache = s; RvipWM.visible($('vis'), s, icon); }

	function onGame(kind, s) {
		switch (kind) {
		case 0: $('status-body').innerHTML = s; break;
		case 1: RvipWM.prompt.text(s); break;
		case 2: {
			const m = $('msg');
			RvipWM.setLog(m, s.split('\n').filter(Boolean).map(l => { const i = l.indexOf('\t'); return { t: l.slice(i + 1), color: l.slice(0, i) }; }));
			m.scrollTop = m.scrollHeight;
			break;
		}
		case 3: showInv(s); break;
		case 4: showVis(s); break;
		case 5: RvipWM.prompt.wait(s === '1'); break;
		case 6: tileNames = s.split('\t'); break;
		case 7:
			tilesMode = s === '1';
			$('btn-tiles').textContent = tilesMode ? 'Tiles: Infra Arcana' : 'Tiles: None';
			break;
		case 8: {
			const p = s.split(' ').map(Number);
			cv = [p[0], p[1]];
			if (p[2]) L.scale = p[2];
			placeCanvas();
			break;
		}
		case 9: showPop(s); break;
		}
	}

	/* ---------- app: saves, export/import, new game, help (rvip-app.js) ---------- */
	const exists = p => { try { return Module.FS.analyzePath(p).exists && Module.FS.stat(p).size > 0; } catch (e) { return false; } };
	const app = RvipApp({
		name: 'ia',
		save: () => exists(SAVE()) ? SAVE() : null,
		clear: () => { try { Module.FS.unlink(SAVE()); } catch (e) { } },
		put: (file, data) => { Module.FS.writeFile(SAVE(), data); },
		exportName: () => 'infra-arcana.sav',
		noSave: 'There is no saved character yet (the game saves when you take the stairs down).',
		helpText: 'Press ? in the game for its manual.'
	});
	window.addEventListener('beforeunload', e => { if (app.running) { app.sync(); e.preventDefault(); e.returnValue = ''; } });
	document.addEventListener('visibilitychange', () => { if (document.hidden) app.sync(); });
	addEventListener('pagehide', () => app.sync());
	setInterval(() => { if (app.running) app.sync(); }, 15000);

	/* ---------- top bar ---------- */
	RvipWM.dropdown($('btn-file'), $('menu-file'));
	RvipWM.dropdown($('btn-audio'), $('menu-audio'));
	$('btn-tiles').onclick = () => { if (app.running && Module._web_toggle_tiles) Module._web_toggle_tiles(); };
	/* sound: stored here, wired to the game in stage 6 */
	$('chk-sound').onchange = function () { L.sound = this.checked; saveLayout(); };
	$('chk-music').onchange = function () { L.music = this.checked; saveLayout(); };
	fetch('fonts.json').then(r => r.json()).then(list => {
		for (const n of list) { const o = document.createElement('option'); o.value = n; o.textContent = n.replace(/^Web(Plus|437)_/, '').replace(/_/g, ' '); $('sel-font').appendChild(o); }
		$('sel-font').value = face;
	}).catch(() => { });
	$('sel-font').onchange = function () { face = L.face = this.value; loadFace(face); saveLayout(); this.blur(); };
	$('sel-font').addEventListener('keydown', e => e.stopPropagation());
	document.querySelectorAll('button').forEach(b => b.addEventListener('mousedown', e => e.preventDefault()));

	/* ---------- the Emscripten module ---------- */
	window.Module = {
		canvas: $('canvas'),
		print: t => console.log(t),
		printErr: t => console.warn(t),
		onGame, onSync: () => app.sync(),
		/* Quit from the main menu */
		onQuit: () => { app.running = false; app.sync(() => setTimeout(() => location.reload(), 800)); },
		onAbort: e => app.crashed(e),
		preRun: [function () {
			Module.addRunDependency('idbfs');
			RvipApp.mount(err => {
				if (err) app.status('Could not open browser storage (IndexedDB): ' + err + '. Saving may not work.', true);
				try { L = JSON.parse(new TextDecoder().decode(Module.FS.readFile(LAYOUT()))) || {}; } catch (e) { L = {}; }
				face = L.face || ''; $('sel-font').value = face; loadFace(face);
				$('chk-sound').checked = !!L.sound; $('chk-music').checked = !!L.music;
				setupWM();
				const b = $('map');
				Module.ENV.IA_USER_DIR = RvipApp.dir + '/';
				Module.ENV.IA_W = String(b.clientWidth || 1000);
				Module.ENV.IA_H = String(b.clientHeight || 700);
				Module.ENV.IA_SCALE = String(L.scale || 1);
				Module.removeRunDependency('idbfs');
			});
		}],
		onRuntimeInitialized: () => { app.running = true; app.status(''); },
	};
	window.iaPage = { app, L: () => L };
})();

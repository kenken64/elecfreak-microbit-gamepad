/*
 * Gamepad blocks: block definitions, the C++ generator and the default
 * program (which reproduces the hand-written firmware).
 *
 * Every gamepad block generates exactly one call into src/v1/gp_runtime.h.
 * Logic and maths use Blockly's built-in blocks with a C++ generator here.
 */
(function (global) {
	'use strict';

	var B = global.Blockly;

	/* micro:bit MakeCode category colours */
	var COLOURS = {
		events: '#1E90FF',
		gamepad: '#D9480F',
		input: '#C400C4',
		stick: '#B22222',
		bt: '#0072E0',
		led: '#5C2D91',
		logic: '#00A4A6',
		math: '#9400D3',
	};

	var AXES = [['left/right (X)', 'X'], ['up/down (Y)', 'Y']];
	var KEYS = [['1', '1'], ['2', '2'], ['3', '3'], ['4', '4'], ['5', '5'], ['6', '6']];
	var BUTTONS = [['1', '1'], ['2', '2'], ['3', '3'], ['4', '4'],
		['5', '5'], ['6', '6'], ['7', '7'], ['8', '8']];
	var AB = [['A', 'A'], ['B', 'B']];
	var ICONS = [['X (cross)', 'CROSS'], ['tick', 'TICK'], ['square', 'SQUARE'], ['! (error)', 'BANG']];
	var PINS = [['P0', '0'], ['P1', '1'], ['P2', '2']];

	var NAME_MAX = 29; /* bytes that fit in the Bluetooth scan response */

	B.Extensions.register('gp_name_validator', function () {
		this.getField('NAME').setValidator(function (v) {
			return String(v).replace(/[^\x20-\x7E]/g, '').slice(0, NAME_MAX);
		});
	});

	function stmt(def) {
		def.previousStatement = null;
		def.nextStatement = null;
		return def;
	}

	B.defineBlocksWithJsonArray([
		/* ---- events ---- */
		{
			type: 'gp_on_start',
			message0: 'on start %1 %2',
			args0: [{type: 'input_dummy'}, {type: 'input_statement', name: 'DO'}],
			colour: COLOURS.events,
			tooltip: 'Runs once at power-on, after the serial log, LED screen and Joystick:bit inputs have started.',
		},
		{
			type: 'gp_every',
			message0: 'every %1 ms %2 %3',
			args0: [
				{type: 'field_number', name: 'MS', value: 10, min: 5, max: 1000, precision: 1},
				{type: 'input_dummy'},
				{type: 'input_statement', name: 'DO'},
			],
			colour: COLOURS.events,
			tooltip: 'Reads keys, buttons and the stick, starts an empty gamepad report, then runs these blocks.',
		},

		/* ---- gamepad report ---- */
		stmt({
			type: 'gp_press',
			message0: 'press gamepad button %1',
			args0: [{type: 'field_dropdown', name: 'BUTTON', options: BUTTONS}],
			colour: COLOURS.gamepad,
			tooltip: 'Marks this gamepad button as pressed in this report (Windows "Button 1" to "Button 8").',
		}),
		stmt({
			type: 'gp_set_axis',
			message0: 'set gamepad %1 to %2',
			args0: [
				{type: 'field_dropdown', name: 'AXIS', options: AXES},
				{type: 'input_value', name: 'VALUE', check: 'Number'},
			],
			inputsInline: true,
			colour: COLOURS.gamepad,
			tooltip: 'Sets a gamepad axis: -127 (left / up) to 127 (right / down).',
		}),
		stmt({
			type: 'gp_send',
			message0: 'send gamepad report',
			colour: COLOURS.bt,
			tooltip: 'Sends the report to the PC, only when it changed and the PC is ready.',
		}),

		/* ---- Joystick:bit inputs ---- */
		{
			type: 'gp_key',
			message0: 'Joystick:bit key %1 is pressed',
			args0: [{type: 'field_dropdown', name: 'KEY', options: KEYS}],
			output: 'Boolean',
			colour: COLOURS.input,
			tooltip: 'One of the six Joystick:bit keys. They share one wire, so only one reads as pressed at a time.',
		},
		{
			type: 'gp_button',
			message0: 'button %1 is pressed',
			args0: [{type: 'field_dropdown', name: 'BTN', options: AB}],
			output: 'Boolean',
			colour: COLOURS.input,
			tooltip: 'micro:bit button A or B.',
		},
		{
			type: 'gp_ab_at_start',
			message0: 'A+B held at power-on',
			output: 'Boolean',
			colour: COLOURS.input,
			tooltip: 'True if A and B were both held while the micro:bit started.',
		},
		stmt({
			type: 'gp_wait_ab',
			message0: 'wait until A and B are released',
			colour: COLOURS.input,
			tooltip: 'Keeps Bluetooth running until A and B are let go, then half a second more.',
		}),

		/* ---- stick ---- */
		{
			type: 'gp_stick',
			message0: 'stick %1 position',
			args0: [{type: 'field_dropdown', name: 'AXIS', options: AXES}],
			output: 'Number',
			colour: COLOURS.stick,
			tooltip: '-127 (left / up) to 127 (right / down), with a dead zone around the centre. 0 while the pin isn\'t connected.',
		},
		{
			type: 'gp_stick_ok',
			message0: 'stick %1 is connected',
			args0: [{type: 'field_dropdown', name: 'AXIS', options: AXES}],
			output: 'Boolean',
			colour: COLOURS.stick,
			tooltip: 'False while the pin fails its connection self-test (a loose contact).',
		},
		{
			type: 'gp_analog',
			message0: 'analog read pin %1',
			args0: [{type: 'field_dropdown', name: 'PIN', options: PINS}],
			output: 'Number',
			colour: COLOURS.stick,
			tooltip: 'Raw reading 0 to 1023.',
		},

		/* ---- Bluetooth ---- */
		stmt({
			type: 'gp_bt_begin',
			message0: 'start Bluetooth gamepad named %1',
			args0: [{type: 'field_input', name: 'NAME', text: 'micro:bit Gamepad'}],
			extensions: ['gp_name_validator'],
			colour: COLOURS.bt,
			tooltip: 'Starts Bluetooth and advertises under this name (up to 29 characters).',
		}),
		stmt({
			type: 'gp_forget',
			message0: 'forget all paired PCs',
			colour: COLOURS.bt,
			tooltip: 'Deletes every saved pairing. Remove the gamepad in Windows too.',
		}),

		/* ---- LEDs ---- */
		stmt({
			type: 'gp_status',
			message0: 'show status on LEDs',
			colour: COLOURS.led,
			tooltip: 'Rings while waiting, a square while pairing, a tick when ready, then the pressed button or a dot for X/Y.',
		}),
		stmt({
			type: 'gp_show_icon',
			message0: 'show icon %1',
			args0: [{type: 'field_dropdown', name: 'ICON', options: ICONS}],
			colour: COLOURS.led,
			tooltip: 'Shows a picture on the 5x5 LEDs.',
		}),
	]);

	/* ------------------------------------------------------------------ */
	/* C++ generator                                                       */
	/* ------------------------------------------------------------------ */

	var Gen = B.CodeGenerator || B.Generator;
	var cpp = new Gen('CPP');
	var O = {ATOMIC: 0, UNARY: 2, MUL: 3, ADD: 4, REL: 6, EQ: 7, AND: 11, OR: 12, NONE: 99};

	cpp.INDENT = '\t';
	cpp.init = function () {
		this.isInitialized = true;
	};
	cpp.finish = function (code) {
		return code;
	};
	cpp.scrub_ = function (block, code, thisOnly) {
		var next = block.nextConnection && block.nextConnection.targetBlock();
		return thisOnly ? code : code + cpp.blockToCode(next);
	};
	cpp.scrubNakedValue = function (line) {
		return line + ';\n';
	};

	function cString(s) {
		return '"' + String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';
	}

	function axisEnum(v) {
		return v === 'Y' ? 'GP_AXIS_Y' : 'GP_AXIS_X';
	}

	var f = cpp.forBlock;

	f.gp_press = function (b) {
		return 'gp_press(' + Number(b.getFieldValue('BUTTON')) + ');\n';
	};
	f.gp_set_axis = function (b, g) {
		var v = g.valueToCode(b, 'VALUE', O.NONE) || '0';
		return 'gp_set_axis(' + axisEnum(b.getFieldValue('AXIS')) + ', ' + v + ');\n';
	};
	f.gp_send = function () {
		return 'gp_report_send();\n';
	};
	f.gp_key = function (b) {
		return ['gp_key_pressed(' + Number(b.getFieldValue('KEY')) + ')', O.ATOMIC];
	};
	f.gp_button = function (b) {
		return ['gp_button_pressed(\'' + (b.getFieldValue('BTN') === 'B' ? 'B' : 'A') + '\')', O.ATOMIC];
	};
	f.gp_ab_at_start = function () {
		return ['gp_ab_held_at_start()', O.ATOMIC];
	};
	f.gp_wait_ab = function () {
		return 'gp_wait_ab_released();\n';
	};
	f.gp_stick = function (b) {
		return ['gp_stick(' + axisEnum(b.getFieldValue('AXIS')) + ')', O.ATOMIC];
	};
	f.gp_stick_ok = function (b) {
		return ['gp_stick_connected(' + axisEnum(b.getFieldValue('AXIS')) + ')', O.ATOMIC];
	};
	f.gp_analog = function (b) {
		return ['gp_analog_read(' + Number(b.getFieldValue('PIN')) + ')', O.ATOMIC];
	};
	f.gp_bt_begin = function (b) {
		return 'gp_bluetooth_begin(' + cString(b.getFieldValue('NAME')) + ');\n';
	};
	f.gp_forget = function () {
		return 'gp_forget_all_pairings();\n';
	};
	f.gp_status = function () {
		return 'gp_status_show();\n';
	};
	f.gp_show_icon = function (b) {
		var icon = {CROSS: 'CROSS', TICK: 'TICK', SQUARE: 'SQUARE', BANG: 'BANG'}[b.getFieldValue('ICON')] || 'CROSS';
		return 'gp_show_icon(GP_ICON_' + icon + ');\n';
	};
	/* Hats only appear as roots; generateMainCpp() reads their bodies. */
	f.gp_on_start = function () {
		return '';
	};
	f.gp_every = function () {
		return '';
	};

	/* Built-in logic and maths */
	f.controls_if = function (b, g) {
		var n = 0;
		var code = '';

		do {
			var cond = g.valueToCode(b, 'IF' + n, O.NONE) || 'false';
			code += (n > 0 ? ' else ' : '') + 'if (' + cond + ') {\n' + g.statementToCode(b, 'DO' + n) + '}';
			n++;
		} while (b.getInput('IF' + n));
		if (b.getInput('ELSE')) {
			code += ' else {\n' + g.statementToCode(b, 'ELSE') + '}';
		}
		return code + '\n';
	};
	f.controls_ifelse = f.controls_if;
	f.logic_compare = function (b, g) {
		var ops = {EQ: '==', NEQ: '!=', LT: '<', LTE: '<=', GT: '>', GTE: '>='};
		var op = ops[b.getFieldValue('OP')];
		var order = (op === '==' || op === '!=') ? O.EQ : O.REL;
		return [(g.valueToCode(b, 'A', order) || '0') + ' ' + op + ' ' + (g.valueToCode(b, 'B', order) || '0'), order];
	};
	f.logic_operation = function (b, g) {
		var and = b.getFieldValue('OP') === 'AND';
		var order = and ? O.AND : O.OR;
		var dflt = and ? 'true' : 'false';
		return [(g.valueToCode(b, 'A', order) || dflt) + (and ? ' && ' : ' || ') + (g.valueToCode(b, 'B', order) || dflt), order];
	};
	f.logic_negate = function (b, g) {
		return ['!' + (g.valueToCode(b, 'BOOL', O.UNARY) || 'true'), O.UNARY];
	};
	f.logic_boolean = function (b) {
		return [b.getFieldValue('BOOL') === 'TRUE' ? 'true' : 'false', O.ATOMIC];
	};
	f.math_number = function (b) {
		var n = Number(b.getFieldValue('NUM'));
		return [String(n), n < 0 ? O.UNARY : O.ATOMIC];
	};
	f.math_arithmetic = function (b, g) {
		var op = b.getFieldValue('OP');
		if (op === 'POWER') {
			return ['(int)pow(' + (g.valueToCode(b, 'A', O.NONE) || '0') + ', ' + (g.valueToCode(b, 'B', O.NONE) || '0') + ')', O.ATOMIC];
		}
		var sym = {ADD: ' + ', MINUS: ' - ', MULTIPLY: ' * ', DIVIDE: ' / '}[op];
		var order = (op === 'ADD' || op === 'MINUS') ? O.ADD : O.MUL;
		/* equal-precedence operands get brackets, so a - (b - c) stays right */
		return [(g.valueToCode(b, 'A', order) || '0') + sym + (g.valueToCode(b, 'B', order) || '0'), order];
	};

	/* ------------------------------------------------------------------ */
	/* Whole-file generation                                               */
	/* ------------------------------------------------------------------ */

	function firstHat(ws, type) {
		var tops = ws.getTopBlocks(true);
		for (var i = 0; i < tops.length; i++) {
			if (tops[i].type === type && tops[i].isEnabled()) {
				return tops[i];
			}
		}
		return null;
	}

	function generateMainCpp(ws) {
		cpp.init(ws);
		var start = firstHat(ws, 'gp_on_start');
		var every = firstHat(ws, 'gp_every');
		var ms = every ? Math.max(5, Math.min(1000, Math.round(Number(every.getFieldValue('MS')) || 10))) : 10;
		var startCode = start ? cpp.statementToCode(start, 'DO') : '';
		var everyCode = every ? cpp.statementToCode(every, 'DO') : '';

		var out = [
			'/*',
			' * main.cpp - GENERATED by the gamepad block editor. Don\'t edit it by hand:',
			' * change the blocks instead (python tools/blocks_editor.py). The blocks are',
			' * saved in blocks/gamepad.json; each one is a gp_*() call from gp_runtime.h.',
			' */',
			'#include <Arduino.h>',
			'',
			'#include "gp_runtime.h"',
			'',
			'/* on start */',
			'void setup()',
			'{',
			'\tgp_boot(); /* serial log, LED screen, Joystick:bit inputs */',
		];
		if (startCode) {
			out.push(startCode.replace(/\n$/, ''));
		}
		out.push(
			'}',
			'',
			'/* every ' + ms + ' ms */',
			'void loop()',
			'{',
			'\tgp_poll(); /* keep Bluetooth running */',
			'\tif (!gp_every(' + ms + ')) {',
			'\t\treturn;',
			'\t}',
			'\tgp_inputs_update(); /* read keys, buttons and stick; start an empty report */'
		);
		if (everyCode) {
			out.push('', everyCode.replace(/\n$/, ''));
		}
		out.push('}', '');
		return out.join('\n');
	}

	/* Warnings for things the generator ignores */
	function checkWorkspace(ws) {
		var seen = {};
		var problems = [];

		ws.getTopBlocks(true).forEach(function (b) {
			if (b.type === 'gp_on_start' || b.type === 'gp_every') {
				if (seen[b.type]) {
					b.setWarningText('Only the first "' + (b.type === 'gp_every' ? 'every' : 'on start') + '" block runs. Remove this one.');
					problems.push('duplicate ' + b.type);
				} else {
					b.setWarningText(null);
					seen[b.type] = true;
				}
			}
		});
		if (!seen.gp_on_start) {
			problems.push('no "on start" block');
		}
		if (!seen.gp_every) {
			problems.push('no "every" block');
		}
		return problems;
	}

	/* ------------------------------------------------------------------ */
	/* Default program = the current firmware                              */
	/* ------------------------------------------------------------------ */

	function chain(blocks) {
		for (var i = blocks.length - 2; i >= 0; i--) {
			blocks[i].next = {block: blocks[i + 1]};
		}
		return blocks[0];
	}

	function num(n) {
		return {shadow: {type: 'math_number', fields: {NUM: n}}};
	}

	function ifBlock(branches, elseBody) {
		/* branches: [[condition, [statements]], ...] */
		var b = {type: 'controls_if', inputs: {}};
		branches.forEach(function (br, i) {
			b.inputs['IF' + i] = {block: br[0]};
			b.inputs['DO' + i] = {block: chain(br[1])};
		});
		if (branches.length > 1 || elseBody) {
			b.extraState = {};
			if (branches.length > 1) {
				b.extraState.elseIfCount = branches.length - 1;
			}
			if (elseBody) {
				b.extraState.hasElse = true;
				b.inputs.ELSE = {block: chain(elseBody)};
			}
		}
		return b;
	}

	function key(k) {
		return {type: 'gp_key', fields: {KEY: String(k)}};
	}

	function button(ab) {
		return {type: 'gp_button', fields: {BTN: ab}};
	}

	function press(n) {
		return {type: 'gp_press', fields: {BUTTON: String(n)}};
	}

	function setAxis(axis, valueBlock, shadowNum) {
		var input = num(shadowNum);
		if (valueBlock) {
			input.block = valueBlock;
		}
		return {type: 'gp_set_axis', fields: {AXIS: axis}, inputs: {VALUE: input}};
	}

	function defaultProgram() {
		var everyBody = [];
		for (var k = 1; k <= 6; k++) {
			everyBody.push(ifBlock([[key(k), [press(k)]]]));
		}
		everyBody.push(setAxis('X', {type: 'gp_stick', fields: {AXIS: 'X'}}, 0));
		everyBody.push(ifBlock(
			[[{type: 'gp_stick_ok', fields: {AXIS: 'Y'}}, [
				setAxis('Y', {type: 'gp_stick', fields: {AXIS: 'Y'}}, 0),
				ifBlock([[button('A'), [press(7)]]]),
				ifBlock([[button('B'), [press(8)]]]),
			]]],
			[ifBlock([
				[button('A'), [setAxis('Y', null, -127)]],
				[button('B'), [setAxis('Y', null, 127)]],
			])]
		));
		everyBody.push({type: 'gp_send'});
		everyBody.push({type: 'gp_status'});

		return {
			blocks: {
				languageVersion: 0,
				blocks: [
					{
						type: 'gp_on_start', x: 30, y: 30,
						inputs: {
							DO: {
								block: chain([
									{type: 'gp_bt_begin', fields: {NAME: 'micro:bit Gamepad'}},
									ifBlock([[{type: 'gp_ab_at_start'}, [
										{type: 'gp_forget'},
										{type: 'gp_show_icon', fields: {ICON: 'CROSS'}},
										{type: 'gp_wait_ab'},
									]]]),
								]),
							},
						},
					},
					{
						type: 'gp_every', x: 560, y: 30, fields: {MS: 10},
						inputs: {DO: {block: chain(everyBody)}},
					},
				],
			},
		};
	}

	/* ------------------------------------------------------------------ */
	/* Toolbox                                                             */
	/* ------------------------------------------------------------------ */

	var TOOLBOX = {
		kind: 'categoryToolbox',
		contents: [
			{kind: 'category', name: 'Events', colour: COLOURS.events, contents: [
				{kind: 'block', type: 'gp_on_start'},
				{kind: 'block', type: 'gp_every'},
			]},
			{kind: 'category', name: 'Gamepad', colour: COLOURS.gamepad, contents: [
				{kind: 'block', type: 'gp_press'},
				{kind: 'block', type: 'gp_set_axis', inputs: {VALUE: num(0)}},
				{kind: 'block', type: 'gp_send'},
			]},
			{kind: 'category', name: 'Keys & buttons', colour: COLOURS.input, contents: [
				{kind: 'block', type: 'gp_key'},
				{kind: 'block', type: 'gp_button'},
				{kind: 'block', type: 'gp_ab_at_start'},
				{kind: 'block', type: 'gp_wait_ab'},
			]},
			{kind: 'category', name: 'Stick', colour: COLOURS.stick, contents: [
				{kind: 'block', type: 'gp_stick'},
				{kind: 'block', type: 'gp_stick_ok'},
				{kind: 'block', type: 'gp_analog'},
			]},
			{kind: 'category', name: 'Bluetooth', colour: COLOURS.bt, contents: [
				{kind: 'block', type: 'gp_bt_begin'},
				{kind: 'block', type: 'gp_forget'},
			]},
			{kind: 'category', name: 'LEDs', colour: COLOURS.led, contents: [
				{kind: 'block', type: 'gp_status'},
				{kind: 'block', type: 'gp_show_icon'},
			]},
			{kind: 'category', name: 'Logic', colour: COLOURS.logic, contents: [
				{kind: 'block', type: 'controls_if'},
				{kind: 'block', type: 'controls_if', extraState: {hasElse: true}},
				{kind: 'block', type: 'logic_compare', inputs: {A: num(0), B: num(0)}},
				{kind: 'block', type: 'logic_operation'},
				{kind: 'block', type: 'logic_negate'},
				{kind: 'block', type: 'logic_boolean'},
			]},
			{kind: 'category', name: 'Math', colour: COLOURS.math, contents: [
				{kind: 'block', type: 'math_number'},
				{kind: 'block', type: 'math_arithmetic', inputs: {A: num(0), B: num(0)}},
			]},
		],
	};

	function theme(dark) {
		return B.Theme.defineTheme(dark ? 'gp-dark' : 'gp-light', {
			base: B.Themes.Classic,
			blockStyles: {
				logic_blocks: {colourPrimary: COLOURS.logic},
				math_blocks: {colourPrimary: COLOURS.math},
			},
			categoryStyles: {},
			componentStyles: dark ? {
				workspaceBackgroundColour: '#141821',
				toolboxBackgroundColour: '#1b202b',
				toolboxForegroundColour: '#e7ebf3',
				flyoutBackgroundColour: '#232a38',
				flyoutForegroundColour: '#e7ebf3',
				flyoutOpacity: 0.97,
				scrollbarColour: '#4b5568',
				insertionMarkerColour: '#ffffff',
				insertionMarkerOpacity: 0.3,
				cursorColour: '#d0d0d0',
			} : {
				workspaceBackgroundColour: '#eef1f6',
				toolboxBackgroundColour: '#f9fafc',
				toolboxForegroundColour: '#1b2130',
				flyoutBackgroundColour: '#e1e6ee',
				flyoutForegroundColour: '#1b2130',
				flyoutOpacity: 0.97,
				scrollbarColour: '#b7c0cf',
				insertionMarkerColour: '#000000',
				insertionMarkerOpacity: 0.2,
			},
			fontStyle: {family: '"Nunito", "Segoe UI", system-ui, sans-serif', weight: '700', size: 11},
			startHats: true,
		});
	}

	global.GP = {
		generateMainCpp: generateMainCpp,
		checkWorkspace: checkWorkspace,
		defaultProgram: defaultProgram,
		toolbox: TOOLBOX,
		theme: theme,
		COLOURS: COLOURS,
	};
})(window);

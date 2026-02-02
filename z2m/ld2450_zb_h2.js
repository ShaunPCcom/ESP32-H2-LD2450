'use strict';

// Zigbee2MQTT external converter for LD2450-ZB-H2 mmWave presence sensor.
// Place this file in your Z2M external_converters directory and restart Z2M.
// After pairing, hit "Reconfigure" in Z2M device settings if entities are missing.

const {Zcl} = require('zigbee-herdsman');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const e = exposes.presets;
const ea = exposes.access;

// ---- Cluster / attribute constants ----

const CLUSTER_CONFIG_ID = 0xFC00;
const CLUSTER_ZONE_ID = 0xFC01;

const ld2450ConfigCluster = {
    ID: CLUSTER_CONFIG_ID,
    attributes: {
        targetCount:    {ID: 0x0000, type: Zcl.DataType.uint8},
        targetCoords:   {ID: 0x0001, type: Zcl.DataType.charStr},
        maxDistance:     {ID: 0x0010, type: Zcl.DataType.uint16},
        angleLeft:      {ID: 0x0011, type: Zcl.DataType.uint8},
        angleRight:     {ID: 0x0012, type: Zcl.DataType.uint8},
        trackingMode:   {ID: 0x0020, type: Zcl.DataType.uint8},
        coordPublishing:{ID: 0x0021, type: Zcl.DataType.uint8},
        restart:        {ID: 0x00F0, type: Zcl.DataType.uint8},
    },
    commands: {},
    commandsResponse: {},
};

const ZONE_ATTRS = ['zoneX1','zoneY1','zoneX2','zoneY2','zoneX3','zoneY3','zoneX4','zoneY4'];
const ZONE_NAMES = ['x1','y1','x2','y2','x3','y3','x4','y4'];

const ld2450ZoneCluster = {
    ID: CLUSTER_ZONE_ID,
    attributes: Object.fromEntries(
        ZONE_ATTRS.map((name, i) => [name, {ID: i, type: Zcl.DataType.int16}])
    ),
    commands: {},
    commandsResponse: {},
};

function registerCustomClusters(device) {
    device.addCustomCluster('ld2450Config', ld2450ConfigCluster);
    device.addCustomCluster('ld2450Zone', ld2450ZoneCluster);
}

// ---- fromZigbee converters ----

const fzLocal = {
    occupancy: {
        cluster: 'msOccupancySensing',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            if (msg.data.occupancy === undefined) return {};
            const val = msg.data.occupancy === 1;
            const ep = msg.endpoint.ID;
            if (ep === 1) return {occupancy: val};
            const zone = ep - 1;
            if (zone >= 1 && zone <= 5) return {[`zone_${zone}_occupancy`]: val};
            return {};
        },
    },

    config: {
        cluster: 'ld2450Config',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            const result = {};
            const d = msg.data;

            if (d.targetCount !== undefined) result.target_count = d.targetCount;
            if (d.maxDistance !== undefined) result.max_distance = d.maxDistance;
            if (d.angleLeft !== undefined) result.angle_left = d.angleLeft;
            if (d.angleRight !== undefined) result.angle_right = d.angleRight;
            if (d.trackingMode !== undefined) {
                result.tracking_mode = d.trackingMode === 1 ? 'single' : 'multi';
            }
            if (d.coordPublishing !== undefined) {
                result.coord_publishing = d.coordPublishing === 1 ? 'ON' : 'OFF';
            }

            if (d.targetCoords !== undefined) {
                const str = d.targetCoords || '';
                const parts = str.split(';').filter(Boolean);
                for (let i = 0; i < 3; i++) {
                    if (i < parts.length) {
                        const [x, y] = parts[i].split(',').map(Number);
                        result[`target_${i + 1}_x`] = isNaN(x) ? 0 : x;
                        result[`target_${i + 1}_y`] = isNaN(y) ? 0 : y;
                    } else {
                        result[`target_${i + 1}_x`] = 0;
                        result[`target_${i + 1}_y`] = 0;
                    }
                }
            }

            return result;
        },
    },

    zone_vertices: {
        cluster: 'ld2450Zone',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            const zone = msg.endpoint.ID - 1;
            if (zone < 1 || zone > 5) return {};
            const result = {};
            for (let i = 0; i < 8; i++) {
                if (msg.data[ZONE_ATTRS[i]] !== undefined) {
                    result[`zone_${zone}_${ZONE_NAMES[i]}`] = msg.data[ZONE_ATTRS[i]];
                }
            }
            return result;
        },
    },
};

// ---- toZigbee converters ----

const tzLocal = {
    config: {
        key: ['max_distance', 'angle_left', 'angle_right', 'tracking_mode', 'coord_publishing'],
        convertSet: async (entity, key, value, meta) => {
            const ep = meta.device.getEndpoint(1);
            const map = {
                max_distance:     {attr: 'maxDistance',      val: (v) => v},
                angle_left:       {attr: 'angleLeft',       val: (v) => v},
                angle_right:      {attr: 'angleRight',      val: (v) => v},
                tracking_mode:    {attr: 'trackingMode',    val: (v) => v === 'single' ? 1 : 0},
                coord_publishing: {attr: 'coordPublishing', val: (v) => v === 'ON' ? 1 : 0},
            };
            const m = map[key];
            await ep.write('ld2450Config', {[m.attr]: m.val(value)});
            return {state: {[key]: value}};
        },
        convertGet: async (entity, key, meta) => {
            const ep = meta.device.getEndpoint(1);
            const attrs = {
                max_distance: 'maxDistance', angle_left: 'angleLeft',
                angle_right: 'angleRight', tracking_mode: 'trackingMode',
                coord_publishing: 'coordPublishing',
            };
            await ep.read('ld2450Config', [attrs[key]]);
        },
    },

    restart: {
        key: ['restart'],
        convertSet: async (entity, key, value, meta) => {
            const ep = meta.device.getEndpoint(1);
            await ep.write('ld2450Config', {restart: 1});
        },
    },

    zone_vertices: {
        key: Array.from({length: 5}, (_, z) =>
            ZONE_NAMES.map(c => `zone_${z + 1}_${c}`)
        ).flat(),
        convertSet: async (entity, key, value, meta) => {
            const m = key.match(/^zone_(\d)_(x|y)(\d)$/);
            if (!m) return;
            const zone = parseInt(m[1]);
            const attrIdx = (parseInt(m[3]) - 1) * 2 + (m[2] === 'y' ? 1 : 0);
            const ep = meta.device.getEndpoint(zone + 1);
            await ep.write('ld2450Zone', {[ZONE_ATTRS[attrIdx]]: value});
            return {state: {[key]: value}};
        },
        convertGet: async (entity, key, meta) => {
            const m = key.match(/^zone_(\d)_(x|y)(\d)$/);
            if (!m) return;
            const ep = meta.device.getEndpoint(parseInt(m[1]) + 1);
            await ep.read('ld2450Zone', ZONE_ATTRS);
        },
    },
};

// ---- Expose definitions ----

const exposesDefinition = [
    e.binary('occupancy', ea.STATE, true, false)
        .withDescription('Overall presence detected in sensing area'),

    ...Array.from({length: 5}, (_, i) =>
        e.binary(`zone_${i + 1}_occupancy`, ea.STATE, true, false)
            .withDescription(`Presence detected in zone ${i + 1}`)
    ),

    e.numeric('target_count', ea.STATE)
        .withValueMin(0).withValueMax(3)
        .withDescription('Number of detected targets'),

    ...Array.from({length: 3}, (_, i) => [
        e.numeric(`target_${i + 1}_x`, ea.STATE).withUnit('mm')
            .withDescription(`Target ${i + 1} X coordinate`),
        e.numeric(`target_${i + 1}_y`, ea.STATE).withUnit('mm')
            .withDescription(`Target ${i + 1} Y coordinate`),
    ]).flat(),

    e.numeric('max_distance', ea.ALL)
        .withValueMin(0).withValueMax(6000).withValueStep(1)
        .withUnit('mm').withDescription('Maximum detection distance'),

    e.numeric('angle_left', ea.ALL)
        .withValueMin(0).withValueMax(90).withValueStep(1)
        .withUnit('°').withDescription('Left angle limit of detection zone'),

    e.numeric('angle_right', ea.ALL)
        .withValueMin(0).withValueMax(90).withValueStep(1)
        .withUnit('°').withDescription('Right angle limit of detection zone'),

    e.enum('tracking_mode', ea.ALL, ['multi', 'single'])
        .withDescription('Multi-target or single-target tracking'),

    e.binary('coord_publishing', ea.ALL, 'ON', 'OFF')
        .withDescription('Enable publishing of target coordinates'),

    e.enum('restart', ea.SET, ['restart'])
        .withDescription('Restart the device'),

    ...Array.from({length: 5}, (_, z) =>
        ZONE_NAMES.map(c =>
            e.numeric(`zone_${z + 1}_${c}`, ea.ALL)
                .withValueMin(-6000).withValueMax(6000).withValueStep(1)
                .withUnit('mm').withDescription(`Zone ${z + 1} vertex ${c}`)
        )
    ).flat(),
];

// ---- Device definition ----

const definition = {
    zigbeeModel: ['LD2450-H2'],
    model: 'LD2450-ZB-H2',
    vendor: 'LD2450Z',
    description: 'HLK-LD2450 mmWave presence sensor (Zigbee, ESP32-H2)',
    fromZigbee: [fzLocal.occupancy, fzLocal.config, fzLocal.zone_vertices],
    toZigbee: [tzLocal.config, tzLocal.restart, tzLocal.zone_vertices],
    exposes: exposesDefinition,
    onEvent: async (type, data, device) => {
        if (type === 'start' || type === 'deviceInterview') {
            registerCustomClusters(device);
        }
    },
    configure: async (device, coordinatorEndpoint) => {
        registerCustomClusters(device);

        // EP 1: main device
        const ep1 = device.getEndpoint(1);
        await ep1.bind('msOccupancySensing', coordinatorEndpoint);
        await ep1.configureReporting('msOccupancySensing', [
            {attribute: 'occupancy', minimumReportInterval: 0,
             maximumReportInterval: 300, reportableChange: 0},
        ]);
        await ep1.bind('ld2450Config', coordinatorEndpoint);
        await ep1.configureReporting('ld2450Config', [
            {attribute: 'targetCount', minimumReportInterval: 0,
             maximumReportInterval: 60, reportableChange: 1},
        ]);
        await ep1.read('ld2450Config', [
            'targetCount', 'maxDistance', 'angleLeft', 'angleRight',
            'trackingMode', 'coordPublishing',
        ]);

        // EPs 2-6: zone endpoints
        for (let z = 0; z < 5; z++) {
            const ep = device.getEndpoint(z + 2);
            await ep.bind('msOccupancySensing', coordinatorEndpoint);
            await ep.configureReporting('msOccupancySensing', [
                {attribute: 'occupancy', minimumReportInterval: 0,
                 maximumReportInterval: 300, reportableChange: 0},
            ]);
            await ep.bind('ld2450Zone', coordinatorEndpoint);
            await ep.read('ld2450Zone', ZONE_ATTRS);
        }
    },
};

module.exports = definition;

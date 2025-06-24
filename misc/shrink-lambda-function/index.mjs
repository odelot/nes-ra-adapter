/**
 * Serves as a proxy to the RetroAchievements API for patch request, but only returns the necessary data for the NES RA Adapter.
 */

import * as https from 'https';

export const handler = async (event) => {
    try {
        let u = "";
        let t = "";
        let g = "";

        let input = event.body;
        let deCodeURLVal = (str) => {
            return decodeURIComponent((str + '').replace(/\+/g, '%20'));
        }
        let getParamsFromBase64EncodedPOSTParams = (input) => {
            let output = {};
            // Decode the base64
            let decoded = atob(input);

            // Split the params by "&"
            let params = decoded.split("&");
            // Turn the params into an object and url decode the values
            params.forEach((keyAndValue) => {
                let keyValueArray = keyAndValue.split("=");
                let key = keyValueArray[0];
                let value = keyValueArray[1];
                output[key] = deCodeURLVal(value);
            });
            return output;
        }
        let postData = getParamsFromBase64EncodedPOSTParams(input);
        u = postData.u;
        t = postData.t;
        g = postData.g;
        g = g.trim();
        t = t.trim();
        u = u.trim();

        // build the url
        const url = "https://retroachievements.org/dorequest.php?r=patch&f=0&u=" + u + "&t=" + t + "&g=" + g;

        // add NES_RA_ADAPTER user agent to the request
        const options = {
            headers: {
                'User-Agent': 'NES_RA_ADAPTER/0.5 rcheevos/11.6'
            }
        };

        if (!url) {
            return {
                statusCode: 400,
                body: JSON.stringify({ error: "Missing 'url' in request" })
            };
        }

        //
        const jsonData = await fetchJson(url, options);

        // remove unnecessary fields for the adapter
        let filteredData = removeFields(jsonData, ["Warning", "RichPresencePatch", "BadgeLockedURL", "BadgeURL", "ImageIconURL", "Rarity", "RarityHardcore", "Author"]);

        // clean string fields
        filteredData = cleanStringFields(filteredData, ["Description"]);
        filteredData = cleanArrayFields(filteredData, ["Leaderboards"]);

        if (filteredData.PatchData && filteredData.PatchData.Achievements) {
            // remove warning
            filteredData.PatchData.Achievements = filteredData.PatchData.Achievements.filter((patch) => patch.Title != "Warning: Unknown Emulator");
            // remove achievements with flags != 3 (not official ones)
            filteredData.PatchData.Achievements = filteredData.PatchData.Achievements.filter((patch) => patch.Flags == 3);
            // if the size of the response is still bigger than 32KB, remove big patches
            if (JSON.stringify(filteredData).length > 32000)
                filteredData.PatchData.Achievements = filteredData.PatchData.Achievements.filter((patch) => patch.MemAddr.length <= 1024);
            // if the size of the response is still bigger than 32KB, remove some achievements, limiting to 60 achievements
            if (JSON.stringify(filteredData).length > 32000)
                filteredData.PatchData.Achievements = filteredData.PatchData.Achievements.filter((patch, i) => i < 60);
            // if the size of the response is still bigger than 32KB, remove some achievements, limiting to 45 achievements
            if (JSON.stringify(filteredData).length > 32000)
                filteredData.PatchData.Achievements = filteredData.PatchData.Achievements.filter((patch, i) => i < 45);
            // if the size of the response is still bigger than 32KB, remove some achievements, limiting to 35 achievements
            if (JSON.stringify(filteredData).length > 32000)
                filteredData.PatchData.Achievements = filteredData.PatchData.Achievements.filter((patch, i) => i < 35);
        }

        return {
            statusCode: 200,
            body: JSON.stringify(filteredData)
        };
    } catch (error) {
        return {
            statusCode: 500,
            body: JSON.stringify({ error: error.message })
        };
    }
};

function fetchJson(url, options) {
    return new Promise((resolve, reject) => {
        https.get(url, options, (res) => {
            let data = '';
            res.on('data', chunk => data += chunk);
            res.on('end', () => {
                try {
                    resolve(JSON.parse(data));
                } catch (error) {
                    reject(new Error("Failed to parse JSON"));
                }
            });
        }).on('error', reject);
    });
}

function removeFields(obj, fields) {
    if (Array.isArray(obj)) {
        return obj.map(item => removeFields(item, fields));
    } else if (typeof obj === 'object' && obj !== null) {
        return Object.fromEntries(
            Object.entries(obj)
                .filter(([key]) => !fields.includes(key))
                .map(([key, value]) => [key, removeFields(value, fields)])
        );
    }
    return obj;
}

function cleanStringFields(obj, fields) {
    if (Array.isArray(obj)) {
        return obj.map(item => cleanStringFields(item, fields));
    } else if (typeof obj === 'object' && obj !== null) {
        return Object.fromEntries(
            Object.entries(obj)
                .map(([key, value]) => [key, (typeof value === 'string' && fields.includes(key)) ? "" : cleanStringFields(value, fields)])
        );
    }
    return obj;
}

function cleanArrayFields(obj, fields) {
    if (Array.isArray(obj)) {
        return obj.map(item => cleanArrayFields(item, fields));
    } else if (typeof obj === 'object' && obj !== null) {
        return Object.fromEntries(
            Object.entries(obj)
                .map(([key, value]) => [key, (Array.isArray(value) && fields.includes(key)) ? [] : cleanArrayFields(value, fields)])
        );
    }
    return obj;
}
/* RetroAchievements relay for the browser build (sh1pc.com/play/).
 *
 * retroachievements.org sends no CORS headers, so a page cannot call its API.
 * firebase.json rewrites /ra/** on the site to this function, which makes the
 * game's requests same-origin and forwards them. Only dorequest.php is
 * relayed -- the one endpoint rcheevos talks to -- so this is not an open
 * proxy. Badge images come from media.retroachievements.org, which does send
 * CORS headers, and never pass through here.
 */
const { onRequest } = require("firebase-functions/v2/https");

const RA_ORIGIN = "https://retroachievements.org/";
/* The desktop build identifies as SilentHillPC/1.0 (pc_ra_http.c); a browser
 * cannot set User-Agent itself, so the relay does it for the web build. */
const USER_AGENT = "SilentHillPC/1.0 (web)";

exports.raRelay = onRequest(
    { region: "us-central1", memory: "128MiB", timeoutSeconds: 30, maxInstances: 10 },
    async (req, res) => {
        const path = req.path.replace(/^\/ra\//, "");
        if (path !== "dorequest.php" || (req.method !== "GET" && req.method !== "POST")) {
            res.status(404).send("not found");
            return;
        }

        const q = req.originalUrl.indexOf("?");
        const url = RA_ORIGIN + path + (q >= 0 ? req.originalUrl.slice(q) : "");
        const init = { method: req.method, headers: { "User-Agent": USER_AGENT } };
        if (req.method === "POST") {
            init.headers["Content-Type"] = req.get("content-type") || "application/x-www-form-urlencoded";
            init.body = req.rawBody;
        }

        try {
            const r = await fetch(url, init);
            const body = Buffer.from(await r.arrayBuffer());
            res.status(r.status)
                .set("Content-Type", r.headers.get("content-type") || "application/json")
                .set("Cache-Control", "no-store")
                .send(body);
        } catch (e) {
            res.status(502).json({ Success: false, Error: "RetroAchievements could not be reached." });
        }
    });

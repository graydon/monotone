
-- this is the "testing" set of lua hooks for monotone
-- it's intended to support self-tests, not for use in
-- production. just overrides some of the std hooks.

function get_passphrase(keyid)
	return keyid
end

function persist_phrase_ok()
	return true
end

function get_post_targets(groupname)
	return { "nntp://127.0.0.1:8119/monotone.test.packets" } 
end

function get_fetch_sources(groupname)
	return { "nntp://127.0.0.1:8119/monotone.test.packets" }
end

function get_news_sender(url, group)
	return "tester@test.com"
end

Features: 
	Pull from origin
	nejde resize okna diff
	přídaná SSH identita nejde být použita stejně jako github účet
	

Implemented in the native follow-up after 0.5.2:
- [x] Pull from origin: direct toolbar menu action, including before a fetch.
- [x] Resize diff: visible horizontal handles and a vertical History divider.
- [x] SSH identity: direct per-repository selection in the account menu without GitHub sign-in, for matching SSH hosts, including GitHub.

GitHub SSH is supported for clone/fetch/pull/push; HTTPS continues to use GitHub OAuth.

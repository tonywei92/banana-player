package bp.dns;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.InetAddress;
import java.net.URI;
import java.net.URLEncoder;
import java.net.UnknownHostException;
import java.net.spi.InetAddressResolver;
import java.net.spi.InetAddressResolverProvider;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Stream;

/**
 * Banana Player's Secure DNS for the Java extensions (Suwayomi-Server and the Tachiyomi extensions it runs): every name
 * the runtime looks up is asked of a DNS-over-HTTPS server instead of the network's resolver, which on some networks
 * answers "no such domain" for manga sites. Loaded by the JDK's resolver SPI (Java 18+, JEP 418) from this jar on the
 * class path; active only when the player passes -Dbp.doh=<url> (its Secure DNS setting), else the built-in resolver
 * answers as if this jar weren't there. The DoH server is reached by its IP literal (https://1.1.1.1/dns-query), so
 * asking it needs no lookup of its own. An answer that fails falls back to the built-in resolver.
 */
public final class DohProvider extends InetAddressResolverProvider
{
	@Override
	public InetAddressResolver get(Configuration configuration)
	{
		InetAddressResolver builtin = configuration.builtinResolver();
		String url = System.getProperty("bp.doh", "");
		if (url.isEmpty()) return builtin;
		return new Doh(url, builtin);
	}

	@Override
	public String name()
	{
		return "Banana Player DoH";
	}

	static final class Doh implements InetAddressResolver
	{
		private final String url;
		private final InetAddressResolver builtin;
		// name -> addresses and when they expire (the answers' own TTLs, at most five minutes)
		private final Map<String, Entry> cache = new ConcurrentHashMap<>();
		private static final Pattern ANSWER = Pattern.compile("\\{[^{}]*?\"type\"\\s*:\\s*(\\d+)[^{}]*?\"TTL\"\\s*:\\s*(\\d+)[^{}]*?\"data\"\\s*:\\s*\"([^\"]*)\"[^{}]*\\}");

		record Entry(List<InetAddress> addresses, long until) {}

		Doh(String url, InetAddressResolver builtin)
		{
			this.url = url;
			this.builtin = builtin;
		}

		@Override
		public Stream<InetAddress> lookupByName(String host, LookupPolicy policy) throws UnknownHostException
		{
			// a literal or a local name: nothing to ask
			if (host.equals("localhost") || host.indexOf(':') >= 0 || host.matches("[0-9.]+")) return builtin.lookupByName(host, policy);
			String key = host.toLowerCase();
			Entry e = cache.get(key);
			long now = System.currentTimeMillis();
			if (e == null || e.until < now)
			{
				try
				{
					List<InetAddress> out = new ArrayList<>();
					long ttl = 300;
					for (int type : new int[] {1, 28})
					{
						String body = ask(host, type);
						Matcher m = ANSWER.matcher(body);
						while (m.find())
						{
							// type 1 (A) and 28 (AAAA) only: a CNAME is followed by the server, its target's records listed after it
							int t = Integer.parseInt(m.group(1));
							if (t != 1 && t != 28) continue;
							ttl = Math.min(ttl, Math.max(30, Long.parseLong(m.group(2))));
							out.add(InetAddress.getByAddress(host, InetAddress.getByName(m.group(3)).getAddress()));
						}
					}
					if (out.isEmpty()) return builtin.lookupByName(host, policy);
					e = new Entry(out, now + ttl * 1000);
					cache.put(key, e);
				}
				catch (Exception ex)
				{
					System.err.println("bp-dns: " + host + " through DoH failed (" + ex.getMessage() + "), asking the network");
					return builtin.lookupByName(host, policy);
				}
			}
			return e.addresses.stream();
		}

		@Override
		public String lookupByAddress(byte[] addr) throws UnknownHostException
		{
			return builtin.lookupByAddress(addr);
		}

		private String ask(String host, int type) throws Exception
		{
			URI u = URI.create(url + "?name=" + URLEncoder.encode(host, StandardCharsets.UTF_8) + "&type=" + type);
			HttpURLConnection c = (HttpURLConnection) u.toURL().openConnection();
			c.setRequestProperty("accept", "application/dns-json");
			c.setConnectTimeout(5000);
			c.setReadTimeout(5000);
			try (InputStream in = c.getInputStream())
			{
				ByteArrayOutputStream b = new ByteArrayOutputStream();
				byte[] buf = new byte[4096];
				int n;
				while ((n = in.read(buf)) > 0 && b.size() < 65536) b.write(buf, 0, n);
				return b.toString(StandardCharsets.UTF_8);
			}
		}
	}
}

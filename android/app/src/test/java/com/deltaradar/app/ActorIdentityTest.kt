package com.deltaradar.app

import org.junit.Assert.*
import org.junit.Test

class ActorIdentityTest {
    @Test fun readsOperatorIdentityAndKeepsUnknownIdsExplicit() {
        assertEquals("Hackclaw", ActorIdentity.label("player", true, 20003, ""))
        assertEquals("Operator 40010", ActorIdentity.label("player", true, 40010, ""))
        assertEquals("Operator 99999", ActorIdentity.label("player", true, 99999, ""))
        assertEquals("Unknown operator", ActorIdentity.label("player", false, 40010, ""))
    }
    @Test fun distinguishesRegularEliteSpecialistsAndAnimalsFromTemplates() {
        assertEquals("Regular AR", ActorIdentity.label("ai", true, 0, "SOL_DT_Basic_AR"))
        assertEquals("Elite Shotgun", ActorIdentity.label("ai", true, 0, "SOL_DT_Elite_Shotgun"))
        assertEquals("Shield 01", ActorIdentity.label("ai", true, 0, "SOL_DT_Shielder_01"))
        assertEquals("Crocodile", ActorIdentity.label("ai", true, 0, "SOL_Animal_Crocodile"))
    }
    @Test fun preservesBossAndGuardDistinctionsWithoutGuessingFromHealth() {
        assertEquals("Boss 01 Guard", ActorIdentity.label("ai", true, 0, "SOL_DT_Boss01_Guard"))
        assertEquals("Boss 02", ActorIdentity.label("ai", true, 0, "SOL_DT_Boss02"))
        assertEquals("Unknown bot", ActorIdentity.label("ai", false, 0, "SOL_DT_Boss02"))
        assertEquals("Unknown bot", ActorIdentity.label("ai", true, 0, "bad\"template"))
    }
    @Test fun supportsOperatorsControlledByBotsAndIndependentTypeLabels() {
        assertEquals("Hackclaw bot", ActorIdentity.label("ai", true, 20003, ""))
        assertEquals("Shepherd", ActorIdentity.labelText("Player", "Shepherd", false, true))
        assertEquals("Player", ActorIdentity.labelText("Player", "Shepherd", true, false))
        assertEquals("Player · Shepherd", ActorIdentity.labelText("Player", "Shepherd", true, true))
        assertEquals("", ActorIdentity.labelText("Player", "Shepherd", false, false))
    }
}

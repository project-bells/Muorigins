-- Give newly created level-1 characters their starting rewards.
-- The level/default-value checks avoid changing imported or seeded characters.
DROP TRIGGER IF EXISTS `CharacterStartingRewards`;

DELIMITER $$

CREATE TRIGGER `CharacterStartingRewards`
BEFORE INSERT ON `Character`
FOR EACH ROW
BEGIN
  IF NEW.cLevel = 1
     AND COALESCE(NEW.LevelUpPoint, 0) = 0
     AND COALESCE(NEW.Money, 0) = 0 THEN
    SET NEW.LevelUpPoint = 300;
    SET NEW.Money = 20000000;
  END IF;
END $$

DELIMITER ;
